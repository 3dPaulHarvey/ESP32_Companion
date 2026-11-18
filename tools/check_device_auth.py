"""Check device authentication without printing the token or sending personal audio.

Defaults to our hosted Worker. The matching token is read from the ignored
cloudflare-worker/.dev.vars file, never from a URL or command-line argument.
"""

import argparse
import json
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request


HOSTED_BASE = "https://esp32-companion-control.stiener.workers.dev"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward a device credential to another URL.
        return None


def validate_base(value):
    try:
        parsed = urllib.parse.urlsplit(value)
        local = parsed.hostname in {"127.0.0.1", "localhost", "::1"}
        if parsed.username or parsed.password or parsed.query or parsed.fragment:
            raise ValueError
        if parsed.path not in {"", "/"} or not parsed.hostname:
            raise ValueError
        if parsed.scheme != "https" and not (parsed.scheme == "http" and local):
            raise ValueError
        parsed.port  # Validate the optional port now.
    except ValueError:
        raise argparse.ArgumentTypeError("Use an HTTPS service origin, or loopback HTTP for a local test.") from None
    return value.rstrip("/")


def load_token():
    path = Path(__file__).resolve().parent.parent / "cloudflare-worker" / ".dev.vars"
    try:
        content = path.read_text(encoding="utf-8")
    except OSError:
        raise RuntimeError("The local .dev.vars credential file is unavailable.") from None
    matches = re.findall(r'^DEVICE_AUTH_TOKEN="([0-9a-f]{64})"[ \t]*$', content, re.M)
    if len(matches) != 1:
        raise RuntimeError("Expected one 64-character device token in .dev.vars.")
    return matches[0]


def check(base, token):
    opener = urllib.request.build_opener(NoRedirect())
    wrong_token = ("0" if token[0] != "0" else "1") + token[1:]
    cases = [
        ("missing credential", None, 401, {"error": "unauthorized"}),
        ("incorrect credential", wrong_token, 401, {"error": "unauthorized"}),
        ("correct credential", token, 200, {"authenticated": True}),
    ]
    results = []
    for label, credential, expected_status, expected_body in cases:
        headers = {"User-Agent": "ESP32-Companion-Auth-Check/0.1"}
        if credential is not None:
            headers["Authorization"] = "Bearer " + credential
        request = urllib.request.Request(base + "/device/check", method="POST", headers=headers)
        try:
            response = opener.open(request, timeout=15)
        except urllib.error.HTTPError as error:
            response = error
        except (urllib.error.URLError, OSError):
            raise RuntimeError("Connection failed; check the service address and TLS/network availability.") from None
        with response:
            status = response.status
            # Read a bounded response but never print or log it: it may be untrusted.
            payload = response.read(4097)
            try:
                body = json.loads(payload) if len(payload) <= 4096 else None
            except (ValueError, UnicodeError):
                body = None
            valid_headers = response.headers.get("Cache-Control") == "no-store"
            valid_headers &= (response.headers.get("Content-Type") or "").startswith("application/json")
            if expected_status == 401:
                valid_headers &= response.headers.get("WWW-Authenticate") == 'Bearer realm="esp32-companion"'
        if status != expected_status or body != expected_body or not valid_headers:
            hint = {
                404: "Deploy the updated authentication handler first.",
                503: "Check the Production device secret and DEVICE_RATE_LIMITER binding.",
                401: "Check that the saved Production secret matches the current local token.",
                429: "Device request limit reached; wait at least 60 seconds before retrying.",
            }.get(status, "Check the deployed handler and authentication configuration.")
            raise RuntimeError(f"{label}: expected HTTP {expected_status} and its JSON contract; got HTTP {status}. {hint}")
        results.append({"case": label, "status": status, "passed": True})
        print(f"PASS {label}: HTTP {status}", flush=True)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", type=validate_base, default=HOSTED_BASE,
                        help="Target service origin; this is where the device credential will be sent.")
    args = parser.parse_args()
    try:
        results = check(args.base_url, load_token())
    except RuntimeError as error:
        parser.exit(1, f"Authentication check failed: {error}\n")
    root = Path(__file__).resolve().parent.parent
    output = root / "logs" / "worker-device-auth-check.log"
    output.parent.mkdir(exist_ok=True)
    output.write_text(json.dumps({"base_url": args.base_url, "checks": results}, indent=2) + "\n", encoding="utf-8")
    print("All authentication checks passed. No token value was printed or logged.")


if __name__ == "__main__":
    main()
