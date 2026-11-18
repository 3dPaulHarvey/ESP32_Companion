"""Measure bounded local microphone captures on COM26; no raw audio or upload."""

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import time

import serial

from check_esp32_voice import VoiceDevice, CheckFailure
from check_esp32_session import EXPECTED_MAC


HEADROOM = (r"MicrophonePrep: HEADROOM request=(\d+) samples=(\d+) pcm_shift=(\d+) raw_peak=(\d+) "
    r"legacy_clipped=(\d+) raw_near_full_scale=(\d+) suggested_shift=(\d+) dc_mean=(-?\d+) ac_rms=(\d+)")
WINDOW = r"MicrophonePrep: WINDOW request=(\d+) second=(\d+) samples=(\d+) rms=(\d+) peak=(\d+) clipped=(\d+)"


def microphone_metadata(capture, text, policy):
    matches = list(re.finditer(HEADROOM, text))
    match = next((item for item in matches if int(item[1]) == capture["request"]), None)
    if not match:
        raise CheckFailure("Missing bounded raw-headroom metadata.")
    headroom = dict(zip(["request", "samples", "pcm_shift", "raw_peak", "legacy_clipped",
        "raw_near_full_scale", "suggested_shift", "dc_mean", "ac_rms"], map(int, match.groups())))
    raw_peak = headroom["raw_peak"]
    suggested = max(12, min(18, (raw_peak - 1).bit_length() - 14)) if raw_peak else 12
    if (headroom["samples"] != capture["samples"] or headroom["pcm_shift"] != policy["pcm_shift"] or
        not 0 <= raw_peak <= 2147483648 or headroom["suggested_shift"] != suggested or
        not capture["clipped"] <= headroom["legacy_clipped"] <= capture["samples"] or
        not 0 <= headroom["raw_near_full_scale"] <= headroom["legacy_clipped"] or
        abs(headroom["dc_mean"]) > capture["rms"] + 1 or headroom["ac_rms"] > capture["rms"] + 1):
        raise CheckFailure("Raw-headroom metadata failed count/gain/level checks.")
    windows = [dict(zip(["request", "second", "samples", "rms", "peak", "clipped"], map(int, item.groups())))
        for item in re.finditer(WINDOW, text) if int(item[1]) == capture["request"]]
    if len(windows) != 4 or [item["second"] for item in windows] != [1, 2, 3, 4]:
        raise CheckFailure("Expected exactly four ordered one-second summaries.")
    if any(item["samples"] != 16000 or not 0 <= item["rms"] <= item["peak"] <= 32768 or
        not 0 <= item["clipped"] <= 16000 for item in windows):
        raise CheckFailure("Invalid microphone window metrics.")
    if (sum(item["samples"] for item in windows) != capture["samples"] or
        sum(item["clipped"] for item in windows) != capture["clipped"] or
        max(item["peak"] for item in windows) != capture["peak"]):
        raise CheckFailure("Window totals do not match the exact capture.")
    combined = math.sqrt(sum(item["rms"] ** 2 for item in windows) / 4)
    if abs(combined - capture["rms"]) > 2:
        raise CheckFailure("Window RMS does not match the aggregate level.")
    return {**capture, **headroom, "windows": windows}


class MicrophoneDevice(VoiceDevice):
    def microphone_feature(self):
        configured, busy = self.protocol()
        if not configured or busy or self.capture["samples"] != 64000:
            raise CheckFailure("The development board is not running the four-second microphone profile at idle.")
        start = self.send(b"s")
        self.wait(r"MicrophonePrep: FEATURE enabled=1 capture_only=1 wake_shift=12 audio_upload=0", start)
        self.wait(r"LocalCompanion: STATUS state=IDLE .*capture=local-4s pending=0", start)
        self.wait(r"CompanionVoice: STATUS active=0 speaking=0 wake_armed=1 playback_busy=0", start)
        return self.capture

    def capture_microphone(self, voice_wake=False, guards=False):
        start = len(self.text) if voice_wake else self.send(b"n")
        if voice_wake:
            self.wait(r"HardwareDiagnostics: WAKE_DETECTED count=\d+", start, timeout=60)
        self.wait(r"LocalCompanion: CAPTURE_BEGIN request=\d+ target_samples=64000 bytes=128000", start)
        if guards:
            self.send(b"ndtv")
        capture = self.wait_local_capture(start, source="voice" if voice_wake else "serial")
        result = microphone_metadata(capture, self.text[start:], self.capture)
        if guards:
            for pattern in [r"LocalCompanion: WAKE_IGNORED source=serial", r"LocalCompanion: COMMAND_IGNORED command=d",
                r"LocalCompanion: COMMAND_IGNORED command=t", r"CompanionVoice: REQUEST_REJECTED reason=local_busy"]:
                self.wait(pattern, start)
        if "REQUEST_BEGIN" in self.text[start:]:
            raise CheckFailure("Unexpected cloud request during a local microphone capture.")
        self.passed("four-second local capture, exact counts and headroom summaries",
            samples=capture["samples"], peak=capture["peak"], clipped=capture["clipped"],
            legacy_clipped=result["legacy_clipped"], suggested_shift=result["suggested_shift"],
            wake_source="voice" if voice_wake else "serial", guards_verified=guards)
        return result

    def cancel_capture(self):
        start = self.send(b"n")
        self.wait(r"LocalCompanion: CAPTURE_BEGIN request=(\d+) target_samples=64000 bytes=128000", start)
        time.sleep(.4)
        self.send(b"x")
        match = self.wait(r"LocalCompanion: CAPTURE_FAILED request=(\d+) samples=(\d+) status=cancelled", start)
        self.wait(r"LocalCompanion: CYCLE_ABORTED .*source=serial reason=cancelled", start)
        self.wait(r"HardwareDiagnostics: WAKE_READY", start)
        if not 0 <= int(match[2]) < 64000:
            raise CheckFailure("Cancelled capture claimed a complete segment.")
        self.passed("capture cancellation clears ownership and rearms wake", partial_samples=int(match[2]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="esp32-microphone-check")
    parser.add_argument("--captures", type=int, choices=[1, 2, 3], default=3)
    parser.add_argument("--condition", choices=["unspecified", "quiet", "voice"], default="unspecified")
    parser.add_argument("--voice-wake", action="store_true", help="Wait for each physical Hi,ESP wake instead of serial n")
    parser.add_argument("--cancel", action="store_true")
    parser.add_argument("--reset", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name): parser.error("Use a controlled result basename.")
    device = MicrophoneDevice(); failure = None; captures = []
    try:
        device.connection.open()
        if args.reset:
            device.reset()
            device.wait(r"CompanionSession: MODE manual-session-probe", 0, timeout=20)
        device.microphone_feature()
        start = device.send(b"s")
        status = device.wait(r"HardwareDiagnostics: STATUS wakes=\d+ WiFi=([a-z]+) connected=([01])", start)
        if status[1] == "on":
            start = device.send(b"o")
            device.wait(r"HardwareDiagnostics: WIFI off; wake remains local;", start)
        device.passed("development identity, four-second policy and offline local mode", **device.capture)
        if args.cancel: device.cancel_capture()
        if args.condition != "unspecified" and not args.voice_wake: time.sleep(3)
        for index in range(args.captures):
            result = device.capture_microphone(voice_wake=args.voice_wake, guards=not args.voice_wake and index == 0)
            captures.append(result)
            print(json.dumps({"capture": index + 1, "condition": args.condition, "rms": result["rms"], "peak": result["peak"],
                "clipped": result["clipped"], "legacy_clipped": result["legacy_clipped"], "raw_peak": result["raw_peak"],
                "suggested_shift": result["suggested_shift"], "window_rms": [item["rms"] for item in result["windows"]]}), flush=True)
            if not args.voice_wake: time.sleep(.5)
        device.microphone_feature()
        device.passed("idle, wake rearmed and capture storage released")
    except (CheckFailure, serial.SerialException, OSError) as error:
        failure = str(error) if isinstance(error, CheckFailure) else "Serial access failed."
        parser.exit(1, "ESP32 microphone check failed: " + failure + "\n")
    finally:
        device.connection.close()
        output = Path(__file__).resolve().parent.parent / "logs" / (args.name + ".json")
        output.parent.mkdir(exist_ok=True)
        output.write_text(json.dumps({"recorded_at": datetime.now(timezone.utc).isoformat(), "client_date": "2026-11-18",
            "device_mac": EXPECTED_MAC, "port": "COM26", "condition": args.condition, "voice_wake": args.voice_wake,
            "policy": device.capture, "captures": captures, "checks": device.checks, "heap_snapshots": device.heaps,
            "failure": failure, "microphone_audio_uploaded": False, "raw_audio_saved": False}, indent=2) + "\n", encoding="utf-8")
    print("Local microphone checks passed; speech conditions/quality require user confirmation.")


if __name__ == "__main__": main()
