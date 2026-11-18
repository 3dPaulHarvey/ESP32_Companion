"""Verify one manual fixed Gemini greeting through COM26; no microphone upload."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re

import serial

from check_esp32_gemini import GeminiDevice
from check_esp32_session import CheckFailure, EXPECTED_MAC


VOICE_RESULT = (r"CompanionVoice: RESULT ok=([01]) http=(\d+) reason=([a-z_]+) "
    r"upgrade_http=(-?\d+) setup_ack=([01]) prompt_sent=([01]) turn_complete=([01]) "
    r"close_sent=([01]) close_ack=([01]) close_code=(\d+) audio_bytes=(\d+) chunks=(\d+) "
    r"sample_rate=24000 played_samples=(\d+) duration_ms=(\d+) rms=(\d+) peak=(\d+) "
    r"clipped=(\d+) playback_ms=(\d+) elapsed_ms=(\d+) cleanup=1 no_audio_upload=1")
FIELDS = ["ok", "http", "reason", "upgrade_http", "setup_ack", "prompt_sent", "turn_complete",
    "close_sent", "close_ack", "close_code", "audio_bytes", "chunks", "played_samples",
    "duration_ms", "rms", "peak", "clipped", "playback_ms", "elapsed_ms"]


def voice_metadata(text, pattern=VOICE_RESULT):
    match = re.search(pattern, text)
    if not match:
        raise CheckFailure("Voice result did not match the controlled contract.")
    values = {key: value if key == "reason" else int(value) for key, value in zip(FIELDS, match.groups())}
    if (values["ok"], values["http"], values["reason"], values["upgrade_http"]) != (1, 200, "verified", 101):
        raise CheckFailure(f"Voice test failed (HTTP {values['http']}, upgrade {values['upgrade_http']}, reason {values['reason']}).")
    if any(values[flag] != 1 for flag in ["setup_ack", "prompt_sent", "turn_complete", "close_sent", "close_ack"]):
        raise CheckFailure("Voice test missed a setup/prompt/completion/closure gate.")
    if values["close_code"] not in {0, 1000, 1001}:
        raise CheckFailure("Voice test returned an unexpected close status.")
    size = values["audio_bytes"]
    if not 2 <= size <= 480000 or size % 2 or values["played_samples"] != size // 2 or values["chunks"] < 1:
        raise CheckFailure("Voice test did not play the exact bounded PCM sample count.")
    if values["duration_ms"] != size // 48 or not 0 < values["rms"] <= values["peak"] <= 32768 or values["clipped"] > size // 2:
        raise CheckFailure("Voice test returned invalid duration/sample metrics.")
    if not max(0, values["duration_ms"] - 100) <= values["playback_ms"] <= values["duration_ms"] + 750:
        raise CheckFailure("Voice playback timing did not match 24 kHz output.")
    return values


class VoiceDevice(GeminiDevice):
    def voice_feature(self):
        start = self.send(b"s")
        self.wait(r"CompanionSession: VOICE_FEATURE enabled=1 fixed_greeting=1 automatic_requests=0 audio_upload=0", start)

    def check_voice(self, guards=False):
        self.voice_stage = "connecting"
        start = self.send(b"v")
        self.wait(r"CompanionVoice: UI state=CONNECTING wake_armed=0", start)
        if guards:
            self.send(b"vndtj")
        self.voice_stage = "voice_result"
        match = self.wait(VOICE_RESULT, start, timeout=90)
        self.checks.append({"case": "manual fixed Gemini greeting", "passed": False,
            "metadata": dict(zip(FIELDS, match.groups()))})
        metadata = voice_metadata(match[0])
        self.voice_stage = "speaking_expression"
        self.wait(r"CompanionVoice: UI state=SPEAKING wake_armed=0", start)
        self.voice_stage = "ready_rearm"
        self.wait(r"CompanionVoice: UI state=READY wake_armed=1", start)
        if guards:
            self.voice_stage = "guard_commands"
            self.wait(r"CompanionVoice: REQUEST_REJECTED reason=local_busy", start)
            self.wait(r"LocalCompanion: WAKE_IGNORED source=serial", start)
            self.wait(r"LocalCompanion: COMMAND_IGNORED command=d", start)
            self.wait(r"LocalCompanion: COMMAND_IGNORED command=t", start)
            self.wait(r"CompanionSession: REQUEST_REJECTED reason=busy", start)
        window = self.text[start:]
        if window.count("CompanionSession: REQUEST_BEGIN kind=voice") != 1 or window.count("CompanionGemini: CONNECT_BEGIN") != 1:
            raise CheckFailure("Voice test opened an unexpected extra cloud request.")
        self.checks.pop()
        self.passed("fixed greeting received, closed, played and returned to READY", **metadata,
            guard_commands_verified=guards, microphone_audio_sent=False)
        configured, busy = self.protocol()
        self.voice_stage = "released_audio_state"
        status = self.send(b"s")
        self.wait(r"CompanionVoice: STATUS active=0 speaking=0 wake_armed=1 playback_busy=0", status)
        if not configured or busy:
            raise CheckFailure("Voice job did not release its control state.")
        self.voice_stage = "complete"

    def check_network_drop(self):
        self.voice_stage = "network_drop"
        start = self.send(b"v")
        self.wait(r"CompanionGemini: CONNECT_BEGIN no_question=0 no_audio_upload=1", start, timeout=40)
        self.send(b"o")
        self.wait(r"HardwareDiagnostics: WIFI off; wake remains local;", start, timeout=20)
        match = self.wait(VOICE_RESULT, start, timeout=60)
        values = {key: value if key == "reason" else int(value) for key, value in zip(FIELDS, match.groups())}
        expected = {"websocket_upgrade_failed", "websocket_read_failed", "setup_send_failed", "prompt_send_failed", "partial_frame_timeout"}
        if values["ok"] or values["played_samples"] or values["turn_complete"] or values["reason"] not in expected:
            raise CheckFailure("Network-loss test did not fail at the expected transport stage.")
        self.wait(r"CompanionVoice: UI state=READY wake_armed=1", start)
        if self.text[start:].count("CompanionSession: REQUEST_BEGIN kind=voice") != 1:
            raise CheckFailure("Unexpected retry after network loss.")
        self.passed("device-side Wi-Fi loss releases voice job and restores wake", **values,
            expected_failure=True, microphone_audio_sent=False)
        self.voice_stage = "complete"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="esp32-voice-hardware-check")
    parser.add_argument("--repeats", type=int, choices=[1, 2, 3], default=1)
    parser.add_argument("--network-drop", action="store_true", help="Also stop device Wi-Fi while a manual connection opens")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        parser.error("Use a controlled result basename.")
    device = VoiceDevice()
    failure = None
    try:
        device.connection.open()
        device.reset()
        device.wait(r"CompanionSession: MODE manual-session-probe", 0, timeout=20)
        configured, busy = device.protocol()
        device.voice_feature()
        if not configured or busy or "REQUEST_BEGIN" in device.text:
            raise CheckFailure("Voice profile is not configured/idle at quiet boot.")
        device.passed("development identity, voice feature and quiet boot")
        start = device.send(b"v")
        device.wait(r"CompanionVoice: REQUEST_REJECTED reason=wifi_required", start)
        device.passed("offline voice command rejected without a cloud call")
        start = device.send(b"w")
        device.wait(r"HardwareDiagnostics: WIFI connected ip=", start, timeout=30)
        device.check_setup()
        for index in range(args.repeats):
            device.check_voice(guards=index == 0)
        if args.network_drop:
            device.check_network_drop()
        else:
            start = device.send(b"o")
            device.wait(r"HardwareDiagnostics: WIFI off; wake remains local;", start)
        start = device.send(b"v")
        device.wait(r"CompanionVoice: REQUEST_REJECTED reason=wifi_required", start)
        start = device.send(b"n")
        device.wait_local_capture(start)
        device.passed("offline local capture/cue recovery after speech")
        device.reset()
        start = len(device.text)
        device.wait(r"CompanionSession: MODE manual-session-probe", start, timeout=20)
        configured, busy = device.protocol()
        device.voice_feature()
        if not configured or busy or "REQUEST_BEGIN" in device.text[start:]:
            raise CheckFailure("Quiet reboot/persistence check failed after speech.")
        device.passed("Wi-Fi-off reboot, credential retained, no automatic request")
    except (CheckFailure, serial.SerialException, OSError) as error:
        failure = str(error) if isinstance(error, CheckFailure) else "Serial access failed."
        parser.exit(1, "ESP32 voice check failed: " + failure + "\n")
    finally:
        device.connection.close()
        output = Path(__file__).resolve().parent.parent / "logs" / (args.name + ".log")
        output.parent.mkdir(exist_ok=True)
        output.write_text(json.dumps({"recorded_at": datetime.now(timezone.utc).isoformat(),
            "port": "COM26", "device_mac": EXPECTED_MAC, "checks": device.checks,
            "heap_snapshots": device.heaps, "failure": failure,
            "failure_stage": getattr(device, "voice_stage", None) if failure else None}, indent=2) + "\n", encoding="utf-8")
    print("All fixed-greeting checks passed; listener confirmation is separate.")


if __name__ == "__main__":
    main()
