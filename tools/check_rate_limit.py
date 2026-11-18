"""Verify burst throttling and optional recovery on our auth-only device check.

Makes at most 35 authenticated burst requests. Reads the ignored local credential,
refuses redirects, and never prints it. This route makes no Gemini requests.
"""

import argparse
import json
from pathlib import Path
import time
import urllib.error
import urllib.request

from check_device_auth import HOSTED_BASE, NoRedirect, load_token, validate_base


def probe(opener, base, path, credential=None, method="POST"):
    headers = {"User-Agent": "ESP32-Companion-Rate-Check/0.1"}
    if credential is not None:
        headers["Authorization"] = "Bearer " + credential
    request = urllib.request.Request(base + path, method=method, headers=headers)
    try:
        response = opener.open(request, timeout=15)
    except urllib.error.HTTPError as error:
        response = error
    except (urllib.error.URLError, OSError):
        raise RuntimeError("Connection failed; check the service address and TLS/network availability.") from None
    with response:
        raw = response.read(4097)
        try:
            body = json.loads(raw) if len(raw) <= 4096 else None
        except (ValueError, UnicodeError):
            body = None
        return {
            "status": response.status,
            "body": body,
            "no_store": response.headers.get("Cache-Control") == "no-store",
            "is_json": (response.headers.get("Content-Type") or "").startswith("application/json"),
            "retry_after": response.headers.get("Retry-After"),
            "revision": response.headers.get("X-Companion-Revision"),
        }


def expect(result, status, body, label, results):
    if result["revision"] != "rate-limit-v2":
        raise RuntimeError("The expected rate-limit revision is not serving this request yet.")
    passed = result["status"] == status and result["body"] == body and result["no_store"] and result["is_json"]
    results.append({"case": label, "status": result["status"], "passed": passed})
    if not passed:
        raise RuntimeError(f"{label}: expected HTTP {status} and its JSON contract; got HTTP {result['status']}.")


def verify(base, token, recover, results):
    opener = urllib.request.build_opener(NoRedirect())
    wrong = ("0" if token[0] != "0" else "1") + token[1:]
    admitted = 0
    for _ in range(35):
        result = probe(opener, base, "/device/check", token)
        if result["status"] == 429:
            expect(result, 429, {"error": "rate_limited"}, "burst throttled", results)
            if result["retry_after"] != "60":
                raise RuntimeError("Throttled response must provide Retry-After: 60.")
            print(f"PASS burst throttled after {admitted} admitted requests; Retry-After=60", flush=True)
            break
        expect(result, 200, {"authenticated": True}, "burst request admitted", results)
        admitted += 1
    else:
        raise RuntimeError("No 429 observed in 35 requests; check the deployed limiter binding and configuration.")

    expect(probe(opener, base, "/device/check"), 401, {"error": "unauthorized"}, "missing credential while throttled", results)
    expect(probe(opener, base, "/device/check", wrong), 401, {"error": "unauthorized"}, "wrong credential while throttled", results)
    expect(probe(opener, base, "/health", method="GET"), 200,
           {"status": "ok", "service": "esp32-companion", "api_version": 1}, "health while throttled", results)
    repeated = probe(opener, base, "/device/check?attempt=another", token)
    expect(repeated, 429, {"error": "rate_limited"}, "changing query does not reset quota", results)
    if repeated["retry_after"] != "60":
        raise RuntimeError("Repeated throttle response must provide Retry-After: 60.")
    print("PASS authentication and health remain available during throttling", flush=True)

    if recover:
        print("Waiting 61 seconds for the rate window before testing recovery...", flush=True)
        deadline = time.monotonic() + 61
        while time.monotonic() < deadline:
            time.sleep(max(0, min(20, deadline - time.monotonic())))
        expect(probe(opener, base, "/device/check", token), 200,
               {"authenticated": True}, "recovered after retry window", results)
        print("PASS authenticated request accepted after the retry window", flush=True)
    elif admitted == 0:
        raise RuntimeError("Service was already throttled; rerun with --recover to prove a successful request.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", type=validate_base, default=HOSTED_BASE,
                        help="Target service origin; the device credential will be sent here.")
    parser.add_argument("--recover", action="store_true", help="Wait one retry window and verify a successful request.")
    args = parser.parse_args()
    results = []
    failure = None
    try:
        verify(args.base_url, load_token(), args.recover, results)
    except RuntimeError as error:
        failure = str(error)
    root = Path(__file__).resolve().parent.parent
    output = root / "logs" / "worker-rate-limit-check.log"
    output.parent.mkdir(exist_ok=True)
    record = {"base_url": args.base_url, "recovery_requested": args.recover, "passed": failure is None, "checks": results}
    if failure:
        record["failure"] = failure
    output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    if failure:
        parser.exit(1, f"Rate-limit check failed: {failure}\n")
    print("Rate-limit checks passed. No credential value was printed or logged.")


if __name__ == "__main__":
    main()
