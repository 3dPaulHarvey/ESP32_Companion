"""Verify the development ESP32's control-only cloud session profile.

The optional provisioning step reads the existing ignored device credential,
sends it in a private USB frame and saves it in the development board's NVS.
No Gemini API key, temporary token or raw serial output is printed or logged.
"""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time

import serial

from check_device_auth import load_token


EXPECTED_MAC = "44:bd:8d:ec:38:e0"


class CheckFailure(Exception):
    pass


class Device:
    def __init__(self):
        self.connection = serial.Serial()
        self.connection.port = "COM26"
        self.connection.baudrate = 115200
        self.connection.timeout = 0.1
        self.connection.write_timeout = 2
        self.connection.dtr = False
        self.connection.rts = False
        self.text = ""
        self.checks = []
        self.heaps = []
        self.credential = None

    def read(self):
        chunk = self.connection.read(self.connection.in_waiting or 1)
        self.text += chunk.decode("utf-8", "replace")
        if len(self.text) > 500000:
            raise CheckFailure("Serial output exceeded the bounded check.")
        if self.credential and self.credential in self.text:
            raise CheckFailure("The device echoed a credential; its output has been suppressed.")
        if any(marker in self.text for marker in ["Guru Meditation", "assert failed:", "Brownout detector", "Stack canary watchpoint"]):
            raise CheckFailure("A device fault occurred; raw serial output has been suppressed.")

    def wait(self, pattern, start, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            match = re.search(pattern, self.text[start:])
            if match:
                return match
            self.read()
        raise CheckFailure("Timed out waiting for the expected device check; no raw serial output logged.")

    def send(self, data):
        start = len(self.text)
        self.connection.write(data)
        self.connection.flush()
        return start

    def passed(self, label, **metadata):
        self.checks.append({"case": label, "passed": True, **metadata})
        print("PASS " + label, flush=True)

    def reset(self):
        self.connection.rts = True
        self.connection.dtr = self.connection.dtr
        time.sleep(0.2)
        self.connection.rts = False
        self.connection.dtr = self.connection.dtr

    def protocol(self):
        start = self.send(b"s")
        match = self.wait(r"CompanionSession: PROTOCOL version=1 mac=([0-9a-f:]+) configured=([01]) busy=([01])", start, timeout=20)
        if match[1] != EXPECTED_MAC:
            raise CheckFailure("The serial device is not the verified development ESP32; no provisioning allowed.")
        heap = re.search(r"HardwareDiagnostics: HEAP internal_free=(\d+) internal_min=(\d+) internal_largest=(\d+) psram_total=(\d+) psram_free=(\d+)", self.text[start:])
        if heap:
            self.heaps.append(dict(zip(["internal_free", "internal_min", "internal_largest", "psram_total", "psram_free"], map(int, heap.groups()))))
        return int(match[2]), int(match[3])

    def probe(self, command, kind, http, ok, reason="verified", overlap_capture=False, reject_busy=False):
        start = self.send((b"n" if overlap_capture else b"") + command * (2 if reject_busy else 1))
        match = self.wait(r"CompanionSession: RESULT kind=" + kind +
            r" ok=([01]) http=(\d+) reason=([a-z_]+) temporary_bytes=(\d+) start_in_s=(\d+) expires_in_s=(\d+) elapsed_ms=(\d+) discarded=1", start, timeout=40)
        if (int(match[1]), int(match[2]), match[3]) != (int(ok), http, reason):
            raise CheckFailure(f"Device {kind} check did not match its expected result (HTTP {int(match[2])}, reason {match[3]}).")
        if http == 200 and not (16 <= int(match[4]) <= 8192 and 0 < int(match[5]) <= 65 and 0 < int(match[6]) <= 305):
            raise CheckFailure("Device session metadata failed size/expiry checks.")
        if reject_busy:
            self.wait(r"CompanionSession: REQUEST_REJECTED reason=busy", start)
        if overlap_capture:
            self.wait(r"LocalCompanion: CAPTURE_COMPLETE request=\d+ samples=32000 bytes=64000 .*sample_rate=16000", start)
            self.wait(r"LocalCompanion: CYCLE_COMPLETE .*source=serial", start)
        self.passed(f"device {kind}: HTTP {http}, {reason}", http=http, elapsed_ms=int(match[7]), busy_guard=reject_busy, local_capture_verified=overlap_capture)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provision", action="store_true", help="Save the current local device credential privately to development-board NVS")
    parser.add_argument("--reset", action="store_true", help="Reset into the profile's Wi-Fi-off startup")
    parser.add_argument("--name", default="esp32-session-check", help="Controlled log basename under logs/")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        parser.error("Use letters/digits/underscores/hyphens for the log name.")
    device = Device()
    try:
        device.connection.open()
        if args.reset:
            device.reset()
            start = len(device.text)
            device.wait(r"CompanionSession: MODE manual-session-probe", start, timeout=20)
        configured, busy = device.protocol()
        if busy:
            raise CheckFailure("A device request is already running; let it finish before this test.")
        device.passed("session profile and development-board identity")
        if args.reset:
            if "REQUEST_BEGIN" in device.text:
                raise CheckFailure("An unexpected automatic request occurred at boot.")
            device.probe(b"g", "session", 0, False, reason="wifi_required")

        if args.provision:
            device.credential = load_token()
            start = device.send(b"~1234\n")
            device.wait(r"CompanionSession: PROVISION ok=0 reason=invalid_frame", start)
            device.passed("incomplete credential frame rejected without writing NVS")
            start = device.send(b"~" + device.credential.encode("ascii") + b"\n")
            device.wait(r"CompanionSession: PROVISION ok=1", start)
            configured, busy = device.protocol()
            if not configured or busy:
                raise CheckFailure("The device did not acknowledge the saved credential.")
            device.passed("private USB credential provisioning")
        elif not configured:
            raise CheckFailure("The development board needs --provision for this profile's first check.")

        start = device.send(b"w")
        device.wait(r"HardwareDiagnostics: WIFI connected ip=", start, timeout=30)
        device.passed("saved-credential Wi-Fi connection")
        device.probe(b"b", "wrong", 401, True, reject_busy=True)
        device.probe(b"u", "missing", 401, True)
        # Ensure local input is idle before overlapping a serial capture with
        # HTTPS. No captured PCM is handed to the session task or network.
        start = device.send(b"s")
        device.wait(r"LocalCompanion: STATUS state=IDLE .*pending=0", start)
        device.probe(b"g", "session", 200, True, overlap_capture=True)
        device.protocol()
        device.probe(b"g", "session", 200, True)
        device.protocol()
        start = device.send(b"o")
        device.wait(r"HardwareDiagnostics: WIFI off; wake remains local;", start)
        device.probe(b"g", "session", 0, False, reason="wifi_required")
        start = device.send(b"n")
        device.wait(r"LocalCompanion: CAPTURE_COMPLETE request=\d+ samples=32000 bytes=64000", start)
        device.wait(r"LocalCompanion: CYCLE_COMPLETE .*source=serial", start)
        device.passed("local capture/cue/idle recovery with Wi-Fi off")
        configured, busy = device.protocol()
        if not configured or busy:
            raise CheckFailure("Final configured/idle status is invalid.")
        # Check persistence and absence of automatic cloud access after reset.
        device.reset()
        start = len(device.text)
        device.wait(r"CompanionSession: MODE manual-session-probe", start, timeout=20)
        configured, busy = device.protocol()
        if not configured or busy or "REQUEST_BEGIN" in device.text[start:]:
            raise CheckFailure("Credential persistence/quiet reboot check failed.")
        device.passed("credential persists; reboot leaves Wi-Fi off and makes no automatic session request")
    except (CheckFailure, RuntimeError, serial.SerialException, OSError) as error:
        # Driver errors and serial bodies can contain private details. Only our
        # controlled failure text is emitted, never an exception stack/body.
        message = str(error) if isinstance(error, (CheckFailure, RuntimeError)) and not isinstance(error, serial.SerialException) else "Serial/credential access failed."
        parser.exit(1, "ESP32 session check failed: " + message + "\n")
    finally:
        device.connection.close()
    output = Path(__file__).resolve().parent.parent / "logs" / (args.name + ".log")
    output.parent.mkdir(exist_ok=True)
    output.write_text(json.dumps({"recorded_at": datetime.now(timezone.utc).isoformat(), "port": "COM26", "device_mac": EXPECTED_MAC, "checks": device.checks, "heap_snapshots": device.heaps}, indent=2) + "\n", encoding="utf-8")
    print("All ESP32 control checks passed; no credential, temporary token or raw serial data logged.")


if __name__ == "__main__":
    main()
