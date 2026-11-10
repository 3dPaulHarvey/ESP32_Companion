"""Verify local state ordering/timing and busy-command guards on the ESP32.

Uses explicitly labelled serial triggers, not microphone/wake recognition.
Full serial evidence remains in the ignored logs directory.
"""

import argparse
from pathlib import Path
import re
import sys
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM26")
    parser.add_argument("--cycles", type=int, default=10, choices=range(1, 21), metavar="1..20")
    parser.add_argument("--disconnect", action="store_true", help="Test one Wi-Fi disconnect during a cycle; station must already be connected")
    parser.add_argument("--cancel", action="store_true", help="Cancel one capture first, then verify normal capture recovery")
    parser.add_argument("--name", default="local-cycles")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        parser.error("--name must contain only letters, digits, underscores or hyphens")

    folder = Path(__file__).resolve().parent.parent / "logs"
    folder.mkdir(exist_ok=True)
    history = []
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.1
    port.dtr = port.rts = False
    port.open()

    def wait_for(pattern, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", "replace")
            if not line:
                continue
            history.append(line)
            clean = re.sub(r"\x1b\[[0-9;]*m", "", line)
            if re.search(r"Guru Meditation|assert failed|watchdog.*triggered", clean, re.I):
                raise AssertionError("Device panic/watchdog during test")
            match = re.search(pattern, clean)
            if match:
                return match
        raise AssertionError(f"Timed out waiting for {pattern}")

    def send(text):
        port.write(text.encode("ascii"))
        port.flush()

    def status(require_connected=False):
        send("s")
        if require_connected:
            wait_for(r"HardwareDiagnostics: STATUS .*WiFi=on connected=1 cloud=disabled")
        return wait_for(r"LocalCompanion: STATUS state=(\w+) started=(\d+) completed=(\d+) voice_started=(\d+) serial_started=(\d+) aborted=(\d+) capture=local-2s pending=(\d+)")

    try:
        port.reset_input_buffer()
        initial = status(require_connected=args.disconnect)
        assert initial[1] == "IDLE", "Start test while robot is idle"
        initial_started, initial_completed = int(initial[2]), int(initial[3])
        initial_aborted = int(initial[6])
        assert initial_started == initial_completed + initial_aborted and initial[7] == '0', "Idle has unfinished capture cleanup"
        initial_voice, initial_serial = int(initial[4]), int(initial[5])
        state_pattern = r"I \((\d+)\) LocalCompanion: STATE (\w+) -> (\w+) cycle=(\d+) source=(\w+)"
        cancelled_cycles = 0
        if args.cancel:
            cancelled_cycle = initial_started + 1
            send("n")
            first = wait_for(state_pattern)
            second = wait_for(state_pattern)
            assert (first[2], first[3], int(first[4]), first[5]) == ('IDLE', 'WAKE_DETECTED', cancelled_cycle, 'serial')
            assert (second[2], second[3], int(second[4]), second[5]) == ('WAKE_DETECTED', 'LISTENING', cancelled_cycle, 'serial')
            send("x")
            wait_for(rf"CAPTURE_FAILED request={cancelled_cycle} samples=\d+ status=cancelled")
            wait_for(rf"CYCLE_ABORTED cycle={cancelled_cycle} aborted={initial_aborted + 1} source=serial reason=cancelled")
            wait_for(r"HardwareDiagnostics: WAKE_READY")
            cancelled_cycles = 1
            print('PASS cancelled capture acknowledged, cycle aborted without claiming success, wake re-armed', flush=True)
        for index in range(args.cycles):
            expected_cycle = initial_started + cancelled_cycles + index + 1
            send("n")
            timestamps = []
            for transition_index, (old, new) in enumerate([
                ("IDLE", "WAKE_DETECTED"), ("WAKE_DETECTED", "LISTENING"),
                ("LISTENING", "SPEAKING"), ("SPEAKING", "IDLE"),
            ]):
                if transition_index == 2:
                    capture = wait_for(rf"CAPTURE_COMPLETE request={expected_cycle} samples=(\d+) bytes=(\d+) duration_ms=(\d+) sample_rate=(\d+) elapsed_ms=(\d+) rms=(\d+) peak=(\d+) clipped=(\d+)")
                    assert tuple(map(int, capture.group(1, 2, 3, 4))) == (32000, 64000, 2000, 16000), capture.group(0)
                    assert 1800 <= int(capture[5]) < 3500, capture.group(0)
                result = wait_for(state_pattern)
                assert (result[2], result[3], int(result[4]), result[5]) == (old, new, expected_cycle, "serial"), result.group(0)
                timestamps.append(int(result[1]))
                if index == 0 and transition_index == 0:
                    # A duplicate trigger and UI/sample commands must not interrupt a cycle.
                    send("ndt" + ("r" if args.disconnect else ""))
            wait_for(rf"LocalCompanion: CYCLE_COMPLETE cycle={expected_cycle} completed={initial_completed + index + 1} source=serial\b")
            for elapsed, minimum in zip([b - a for a, b in zip(timestamps, timestamps[1:])], [100, 1800, 800]):
                assert elapsed >= minimum - 20, ("State advanced early", timestamps)
            print(f"PASS serial capture {index + 1}/{args.cycles}: 32,000 samples, 64,000 bytes, completion-driven states", flush=True)

        text = "".join(history)
        assert "WAKE_IGNORED source=serial" in text, "Busy trigger was not rejected"
        assert "COMMAND_IGNORED command=d" in text and "COMMAND_IGNORED command=t" in text, "Busy UI/cue command was not rejected"
        if args.disconnect:
            recovery_pattern = r"WIFI intentional disconnect; testing automatic retry.*?WIFI event=disconnected.*?WIFI event=connected.*?WIFI connected ip="
            if not re.search(recovery_pattern, text, re.S):
                wait_for(r"HardwareDiagnostics: WIFI connected ip=", timeout=20)
            assert re.search(recovery_pattern, "".join(history), re.S), "Ordered disconnect/reconnect/IP recovery was not observed"
            print("PASS Wi-Fi disconnect/reconnect without resetting local cycle", flush=True)
        final = status(require_connected=args.disconnect)
        assert final[1] == "IDLE"
        assert int(final[2]) == initial_started + cancelled_cycles + args.cycles
        assert int(final[3]) == initial_completed + args.cycles
        assert int(final[4]) == initial_voice, "Unexpected real wake during synthetic test; repeat without speaking"
        assert int(final[5]) == initial_serial + cancelled_cycles + args.cycles
        assert int(final[6]) == initial_aborted + cancelled_cycles and final[7] == '0'
        print("PASS success/abort counters, cleanup, IDLE recovery and busy guards; synthetic triggers only", flush=True)
    finally:
        port.close()
        (folder / f"{args.name}-private.log").write_text("".join(history), encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, serial.SerialException) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
