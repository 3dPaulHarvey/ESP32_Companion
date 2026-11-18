"""Check direct ESP32 Gemini setup/close without questions or audio upload."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re

import serial

from check_esp32_session import CheckFailure, Device, EXPECTED_MAC


SETUP_RESULT = (r"CompanionGemini: RESULT ok=([01]) upgrade_http=(-?\d+) setup_ack=([01]) "
    r"close_sent=([01]) close_ack=([01]) close_code=(\d+) ack_bytes=(\d+) elapsed_ms=(\d+) "
    r"cleanup=1 no_question=1 no_audio_upload=1")


def setup_metadata(text):
    match = re.search(SETUP_RESULT, text)
    if not match:
        raise CheckFailure("The direct Gemini result did not match the controlled contract.")
    values = dict(zip(["ok", "upgrade_http", "setup_ack", "close_sent", "close_ack", "close_code", "ack_bytes", "elapsed_ms"], map(int, match.groups())))
    if (values["ok"], values["upgrade_http"], values["setup_ack"], values["close_sent"], values["close_ack"]) != (1, 101, 1, 1, 1):
        raise CheckFailure("Gemini setup/closure failed "
            f"(upgrade={values['upgrade_http']}, setup={values['setup_ack']}, "
            f"close_sent={values['close_sent']}, close_ack={values['close_ack']}, "
            f"close_code={values['close_code']}, elapsed_ms={values['elapsed_ms']}).")
    if values["close_code"] not in {0, 1000, 1001} or not 1 <= values["ack_bytes"] <= 16384:
        raise CheckFailure("The direct Gemini closure/acknowledgment metadata is invalid.")
    return values


class GeminiDevice(Device):
    def feature(self):
        start = self.send(b"s")
        self.wait(r"CompanionSession: FEATURES gemini_setup=1 automatic_requests=0 audio_upload=0", start)

    def check_setup(self, overlap=False, busy_guard=False):
        start = self.send(b"jj" if busy_guard else b"j")
        if overlap:
            first = self.wait(r"CompanionGemini: CONNECT_BEGIN|CompanionSession: RESULT kind=gemini ok=0 http=\d+ reason=[a-z_]+", start, timeout=40)
            if "CONNECT_BEGIN" not in first[0]:
                raise CheckFailure("The broker session failed before direct Gemini connection.")
            self.send(b"n")
        match = self.wait(SETUP_RESULT, start, timeout=60)
        broker = self.wait(r"CompanionSession: RESULT kind=gemini ok=([01]) http=(\d+) reason=([a-z_]+) temporary_bytes=(\d+) start_in_s=(\d+) expires_in_s=(\d+) elapsed_ms=(\d+) discarded=1", start)
        try:
            metadata = setup_metadata(match[0])
        except CheckFailure as error:
            self.checks.append({"case": "direct Gemini setup/close", "passed": False,
                "reason": broker[3], "metadata": dict(zip(
                    ["ok", "upgrade_http", "setup_ack", "close_sent", "close_ack", "close_code", "ack_bytes", "elapsed_ms"],
                    map(int, match.groups())))})
            raise CheckFailure(str(error) + " Device reason=" + broker[3] + ".") from None
        if (broker[1], broker[2], broker[3]) != ("1", "200", "verified") or not 16 <= int(broker[4]) <= 8192:
            raise CheckFailure("The direct Gemini check did not retain the verified broker contract.")
        if busy_guard:
            self.wait(r"CompanionSession: REQUEST_REJECTED reason=busy", start)
        if overlap:
            self.wait(r"LocalCompanion: CAPTURE_COMPLETE request=\d+ samples=32000 bytes=64000 .*sample_rate=16000", start)
            self.wait(r"LocalCompanion: CYCLE_COMPLETE .*source=serial", start)
        window = self.text[start:]
        if window.count("CompanionSession: REQUEST_BEGIN kind=gemini") != 1 or window.count("CompanionGemini: CONNECT_BEGIN") != 1:
            raise CheckFailure("Unexpected duplicate or automatic connection attempt.")
        self.passed("direct Gemini upgrade/setup/close and cleanup", **metadata,
            local_capture_verified=overlap, busy_guard=busy_guard, questions_sent=0, audio_messages_sent=0)
        configured, busy = self.protocol()
        if not configured or busy:
            raise CheckFailure("The device did not return to configured/idle control state.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="esp32-gemini-setup-check")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        parser.error("Use a controlled log basename.")
    device = GeminiDevice()
    failure = None
    try:
        device.connection.open()
        device.reset()
        device.wait(r"CompanionSession: MODE manual-session-probe", 0, timeout=20)
        configured, busy = device.protocol()
        if not configured or busy:
            raise CheckFailure("Provision the development credential with the session checker first.")
        device.feature()
        if "REQUEST_BEGIN" in device.text:
            raise CheckFailure("Unexpected automatic request during boot.")
        device.passed("Gemini profile, development identity and quiet boot")
        start = len(device.text)
        device.probe(b"j", "gemini", 0, False, reason="wifi_required")
        if "CompanionGemini: CONNECT_BEGIN" in device.text[start:]:
            raise CheckFailure("Wi-Fi-off check unexpectedly opened a transport.")
        start = device.send(b"w")
        device.wait(r"HardwareDiagnostics: WIFI connected ip=", start, timeout=30)
        device.passed("saved-credential Wi-Fi connection")
        device.probe(b"b", "wrong", 401, True)
        device.probe(b"u", "missing", 401, True)
        device.probe(b"g", "session", 200, True)
        device.check_setup(overlap=True, busy_guard=True)
        device.check_setup()
        device.check_setup()
        start = device.send(b"o")
        device.wait(r"HardwareDiagnostics: WIFI off; wake remains local;", start)
        device.probe(b"j", "gemini", 0, False, reason="wifi_required")
        start = device.send(b"n")
        device.wait(r"LocalCompanion: CAPTURE_COMPLETE request=\d+ samples=32000 bytes=64000", start)
        device.wait(r"LocalCompanion: CYCLE_COMPLETE .*source=serial", start)
        device.passed("local capture/cue recovery after transport shutdown")
        device.reset()
        start = len(device.text)
        device.wait(r"CompanionSession: MODE manual-session-probe", start, timeout=20)
        configured, busy = device.protocol()
        device.feature()
        if not configured or busy or "REQUEST_BEGIN" in device.text[start:]:
            raise CheckFailure("Quiet reboot/persistence check failed.")
        device.passed("quiet Wi-Fi-off reboot with credential retained")
    except (CheckFailure, serial.SerialException, OSError) as error:
        message = str(error) if isinstance(error, CheckFailure) else "Serial access failed."
        failure = message
        parser.exit(1, "ESP32 Gemini setup check failed: " + message + "\n")
    finally:
        device.connection.close()
        root = Path(__file__).resolve().parent.parent
        (root / "logs").mkdir(exist_ok=True)
        (root / "logs" / (args.name + ".log")).write_text(json.dumps({
            "recorded_at": datetime.now(timezone.utc).isoformat(), "port": "COM26", "device_mac": EXPECTED_MAC,
            "checks": device.checks, "heap_snapshots": device.heaps, "failure": failure,
        }, indent=2) + "\n", encoding="utf-8")
    print("All direct Gemini setup checks passed; no question, audio or credential value logged.")


if __name__ == "__main__":
    main()
