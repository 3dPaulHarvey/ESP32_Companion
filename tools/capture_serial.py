"""Capture a bounded ESP32 boot log; print only hardware/state diagnostics.

Full output stays in the Git-ignored logs directory. No audio is captured.
"""

import argparse
from pathlib import Path
import re
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM26")
    parser.add_argument("--seconds", type=int, default=30, choices=range(1, 61), metavar="1..60")
    parser.add_argument("--reset", action="store_true", help="Reset device before capture")
    parser.add_argument("--name", default="boot", help="Log basename under logs/")
    parser.add_argument("--send", choices=list("hnxdtwors"), help="Send one diagnostic menu command")
    parser.add_argument("--send-after", type=float, default=0.5, help="Seconds before sending the command")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        parser.error("--name must contain only letters, digits, underscores or hyphens")
    if args.send and not 0 <= args.send_after < args.seconds:
        parser.error("--send-after must be within the capture duration")

    output = Path(__file__).resolve().parent.parent / "logs"
    output.mkdir(exist_ok=True)
    connection = serial.Serial()
    connection.port = args.port
    connection.baudrate = 115200
    connection.timeout = 0.2
    connection.dtr = False
    connection.rts = False
    connection.open()
    chunks = []
    try:
        if args.reset:
            connection.rts = True
            # usbser.sys needs DTR reapplied to transmit an RTS change.
            # This matches the installed Espressif monitor reset primitive.
            connection.dtr = connection.dtr
            time.sleep(0.2)
            connection.rts = False
            connection.dtr = connection.dtr
        started = time.monotonic()
        deadline = started + args.seconds
        sent = False
        while time.monotonic() < deadline:
            if args.send and not sent and time.monotonic() >= started + args.send_after:
                connection.write(args.send.encode("ascii"))
                connection.flush()
                sent = True
                print(f"Sent diagnostic command: {args.send}", flush=True)
            chunks.append(connection.read(connection.in_waiting or 1))
    finally:
        connection.close()

    raw = b"".join(chunks).decode("utf-8", "replace")
    (output / f"{args.name}-private.log").write_text(raw, encoding="utf-8")
    safe_markers = (
        "esp_psram:", "esp_image:", "app_init:", "spi_flash:",
        "SeeedStudioS3WifiBoard:", "OledDisplay:", "NoAudioCodec:",
        "AudioCodec:", "AfeWakeWord:", "Wake word detected:", "State:",
        "free heap", "SystemInfo: free sram:",
        "HardwareDiagnostics:", "MicDiagnostics:",
        "LocalCompanion:",
        "Guru Meditation", "Backtrace:", "assert failed:",
    )
    blocked = ("password", "token", "authorization", "secret", "ssid", "transcript")
    summary = []
    for line in raw.splitlines():
        line = re.sub(r"\x1b\[[0-9;]*m", "", line)
        if any(marker in line for marker in safe_markers) and not any(word in line.lower() for word in blocked):
            summary.append(line)
    text = "\n".join(summary) + "\n"
    (output / f"{args.name}-summary.log").write_text(text, encoding="utf-8")
    print(text, end="")
    print(f"Captured {len(raw)} characters on {args.port}; full log remains under logs/.")


if __name__ == "__main__":
    main()
