"""Read-only COM26 observer for live conversations; saves bounded metadata only.

The observer never arms the robot or starts a cloud request. It sends only the
existing status command and leaves microphone/wake ownership on the device.
"""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import sys
import time

import serial

from check_esp32_microphone import MicrophoneDevice
from check_esp32_session import CheckFailure, EXPECTED_MAC


MAX_LINE = 4096
MAX_EVENTS = 8192
REASONS = frozenset("""
    none ok verified complete success ready quiet quiet_timeout silence speech_end
    speech_ended duration_limit capture_limit turn_limit idle_timeout idle_expired
    session_timeout session_expired cancelled wifi_lost wifi_required wifi_off
    offline unconfigured credential_required cooldown retry_window busy
    control_paused diagnostic_pause owner_expired capture_failed capture_timeout
    capture_unavailable capture_cancelled read_error unsupported_format
    allocation_failed vad_allocation_failed invalid_input invalid_vad_frames
    audio_encode_failed audio_send_failed audio_end_failed audio_upload_timeout
    websocket_upgrade_failed websocket_read_failed invalid_frame_state
    partial_frame_timeout invalid_or_oversized_frame oversized_message
    invalid_frame_length response_limit unexpected_setup_response setup_timeout
    setup_send_failed tls_init_failed writer_init_failed websocket_init_failed
    websocket_config_failed invalid_temporary_credential pong_send_failed
    unexpected_server_close close_send_failed close_timeout unexpected_close_code
    write_stream_aborted playback_task_failed playback_failed turn_timeout
    invalid_or_complex_json unexpected_server_message invalid_server_message
    content_after_turn content_after_generation invalid_turn_flag turn_interrupted
    invalid_model_turn invalid_model_role invalid_parts unexpected_part
    invalid_audio_format audio_limit invalid_or_excessive_pcm empty_audio_turn
    input_overflow output_overflow queue_overflow buffer_overflow stream_failed
    provider_close go_away normal normal_close ended no_speech limit warm_expired
    time_sync_failed https_open_failed headers_failed rate_limited
    unexpected_http_status unexpected_headers oversized_response response_timeout
    response_read_failed invalid_session_response session_window_too_short
    invalid_capture_prefix unsolicited_audio incomplete_live_turn session_limit
    provider_going_away utterance_limit fixture_speech fixture_timeout socket_config_failed
""".split())
ENUMS = {
    "reason": REASONS,
    "status": REASONS | {"listening", "recording", "connecting", "speaking", "warm", "idle", "timeout", "stopped", "fatal", "invalid"},
    "state": frozenset({"READY", "ASK", "ASK_NOW", "LISTENING", "CONNECTING", "SPEAKING", "WARM", "IDLE", "WIFI", "WAIT", "SETUP"}),
    "mode": frozenset({"standalone", "live", "autonomous", "desktop"}),
    "owner": frozenset({"standalone", "live", "autonomous", "desktop"}),
    "source": frozenset({"wake", "voice", "followup", "warm_wake", "standalone"}),
    "injected": frozenset({"header_wait", "header_short", "payload_short", "payload_want_write"}),
}
SCHEMAS = {
    "OWNER": "job mode owner heartbeat_required",
    "FEATURE": "version enabled auto_wake boot_requests stream_input stream_output max_turns quiet_ms silence_ms max_capture_ms idle_ms session_ms pcm_shift wake_shift input_rate output_rate sample_rate",
    "LIVE_FEATURE": "version connection_during_capture warm_wake automatic_replay",
    "LIVE_STATUS": "active pending_capture stage turn",
    "LIVE_FIXTURE_BEGIN": "reused microphone",
    "STATUS": "active busy ready warm turn job turns completed_turns wake_armed remaining_ms idle_ms configured wifi paused cooldown_ms admission_open",
    "UI": "state turn job request wake_armed warm mono_us",
    "TRIGGER": "request wake_count reused mono_us",
    "CAPTURE": "job turn request samples captured_samples published_samples uploaded_samples discarded_samples frames speech_frames longest accepted endpoint reason status duration_ms elapsed_ms endpoint_ms queue_max queue_capacity queue_high_water queue_overflows quiet audio_uploaded mono_us start_us end_us speech_end_us pre_roll_samples pcm_shift clipped rms peak",
    "INPUT": "job turn request bytes chunks frames samples stream_ended ended input_cleared elapsed_ms upload_ms live captured_samples discarded_samples",
    "TURN": "job turn complete audio_bytes chunks sample_rate played_samples duration_ms rms peak clipped playback_ms underruns prebuffer_ms queue_max queue_high_water queue_capacity streamed streaming mono_us",
    "TIMING": "job turn capture_ms upload_ms first_audio_ms first_play_ms first_pcm_ms generation_complete_ms turn_complete_ms playback_start_ms elapsed_ms connect_ms setup_ms ready_ms upgrade_ms end_to_speech_ms end_to_playback_ms mono_us speech_end_us start_us end_us capture_end_us first_audio_us playback_start_us first_play_us",
    "RESULT": "job ok http reason upgrade_http setup_ack close_sent close_ack close_code turns completed_turns quiet_discards elapsed_ms cleanup same_connection fixture captures followup_captures input_bytes played_samples streaming session_ms",
    "WARM": "job ready warm idle_ms remaining_ms turns wake_armed state reason timeout_ms connection_open",
    "WAKE": "job turn count warm reused source mono_us",
    "CONNECTION_TIMING": "job upgrade_ms ready_ms elapsed_ms reused tcp_nodelay",
    "DISCARD": "job turn reason samples bytes audio_uploaded input_cleared",
    "CLOSE": "job reason sent ack close_code elapsed_ms",
    "UPLOAD_HEALTH": "turn bytes chunks ended upload_ms max_send_ms slow_sends",
    "WRITE_STATS": "calls waits partials question_retries",
    "CLEANUP": "reason",
    "SEND_FAILURE": "turn opcode expected returned elapsed_ms system_errno socket_errno tls_status tls_error tls_flags input_bytes input_chunks wifi rssi internal_free",
    "WRITE_FAILURE": "status written calls waits partials last_return",
    "WRITE_FAULT": "injected",
}
SCHEMAS = {event: frozenset(fields.split()) for event, fields in SCHEMAS.items()}
BOOL_FIELDS = frozenset("enabled auto_wake heartbeat_required active busy ready warm wake_armed configured wifi paused admission_open accepted quiet audio_uploaded stream_ended ended input_cleared live complete streamed streaming ok setup_ack close_sent close_ack cleanup same_connection fixture reused sent ack stream_input stream_output connection_during_capture warm_wake automatic_replay connection_open".split())
BOOL_FIELDS = BOOL_FIELDS | {"microphone", "tcp_nodelay"}
SIGNED_FIELDS = frozenset("returned system_errno socket_errno tls_status tls_error rssi last_return".split())
ANSI = re.compile(r"\x1b\[[0-9;]*m")
LINE = re.compile(r"^(?:(?:I|W|E) \((\d+)\) )?LiveConversation: ([A-Z][A-Z0-9_]*) (.+)$")


def parse_live_line(line):
    """Parse one complete log line; unknown live fields fail without echoing it."""
    line = ANSI.sub("", line).strip("\r")
    if "LiveConversation:" not in line:
        return None
    if len(line) > MAX_LINE:
        raise CheckFailure("Live event exceeded its size bound; contents suppressed.")
    match = LINE.fullmatch(line)
    if not match or match[2] not in SCHEMAS:
        raise CheckFailure("Unrecognized live event; contents suppressed.")
    event, fields = match[2], {}
    for item in match[3].split(" "):
        if item.count("=") != 1:
            raise CheckFailure("Malformed live event; contents suppressed.")
        key, value = item.split("=", 1)
        if key not in SCHEMAS[event] or key in fields:
            raise CheckFailure("Unexpected live event field; contents suppressed.")
        if key in ENUMS:
            if value not in ENUMS[key]:
                raise CheckFailure("Unexpected live event value; contents suppressed.")
            fields[key] = value
        else:
            pattern = r"0|-?[1-9][0-9]{0,18}" if key in SIGNED_FIELDS else r"0|[1-9][0-9]{0,18}"
            if not re.fullmatch(pattern, value):
                raise CheckFailure(f"Invalid live numeric metadata ({event}.{key}); contents suppressed.")
            number = int(value)
            if not -(2**63) <= number <= 2**63 - 1 or (key in BOOL_FIELDS and number not in {0, 1}):
                raise CheckFailure("Out-of-range live numeric metadata; contents suppressed.")
            if (key == "turn" and number > 8) or (key == "sample_rate" and number not in {16000, 24000}):
                raise CheckFailure("Live metadata exceeded its controlled policy; contents suppressed.")
            fields[key] = number
    result = {"event": event, "fields": fields}
    if match[1] is not None:
        device_ms = int(match[1])
        if device_ms > 2**63 - 1:
            raise CheckFailure("Invalid live device clock; contents suppressed.")
        result["device_ms"] = device_ms
    return result


class LiveLines:
    """Frame serial pieces without saving raw text or incomplete messages."""
    def __init__(self):
        self.pending = ""

    def feed(self, piece):
        self.pending += piece
        events = []
        while "\n" in self.pending:
            line, self.pending = self.pending.split("\n", 1)
            if len(line) > MAX_LINE:
                raise CheckFailure("Serial message exceeded the observer bound; contents suppressed.")
            event = parse_live_line(line)
            if event:
                events.append(event)
        if len(self.pending) > MAX_LINE:
            raise CheckFailure("Partial serial message exceeded the observer bound; contents suppressed.")
        return events


class ReadOnlyLiveDevice(MicrophoneDevice):
    def send(self, data):
        if data != b"s":
            raise CheckFailure("The live observer can only query status.")
        return super().send(data)

    def reset(self):
        raise CheckFailure("The live observer cannot reset the robot.")


class LiveReport:
    def __init__(self, path):
        self.path = Path(path)
        self.data = {
            "schema_version": 1, "recorded_at": datetime.now(timezone.utc).isoformat(),
            "device_mac": EXPECTED_MAC, "port": "COM26", "observer_only": True,
            "raw_audio_saved": False, "transcripts_saved": False, "raw_serial_saved": False,
            "status_commands": 0, "finished": False, "elapsed_ms": 0, "failure": None, "events": [],
        }

    def add(self, events):
        if len(self.data["events"]) + len(events) > MAX_EVENTS:
            raise CheckFailure("Live event count exceeded the observer bound.")
        self.data["events"].extend(events)

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(self.path.name + ".tmp")
        with temporary.open("w", encoding="utf-8", newline="\n") as output:
            json.dump(self.data, output, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        for attempt in range(6):
            try:
                os.replace(temporary, self.path)
                break
            except PermissionError:
                if attempt == 5:
                    raise
                time.sleep(.05 * (attempt + 1))


def observe(seconds, report, *, device=None):
    device = device or ReadOnlyLiveDevice()
    framing = LiveLines()
    started = time.monotonic()
    saved_at = started
    failure = None
    report.save()  # Persist even a connection failure or interrupted preflight.
    try:
        device.connection.open()
        configured, _ = device.protocol(require_capture=True)
        report.data["status_commands"] += 1
        if not configured:
            raise CheckFailure("The verified robot is missing its device configuration.")
        report.add(framing.feed(device.text))
        device.text = device.text[-MAX_LINE:]
        report.save()
        deadline = time.monotonic() + seconds
        next_status = time.monotonic() + 5
        while time.monotonic() < deadline:
            if time.monotonic() >= next_status:
                device.send(b"s")
                report.data["status_commands"] += 1
                next_status = time.monotonic() + 5
            previous = len(device.text)
            device.read()  # Existing identity/fault/privacy guards remain active.
            events = framing.feed(device.text[previous:])
            report.add(events)
            for event in events:
                if event["event"] in {"UI", "TRIGGER", "CAPTURE", "INPUT", "TURN", "RESULT", "WARM"}:
                    print(json.dumps(event, separators=(",", ":")), flush=True)
            device.text = device.text[-MAX_LINE:]
            report.data["elapsed_ms"] = int((time.monotonic() - started) * 1000)
            if events or time.monotonic() - saved_at >= 1:
                report.save()
                saved_at = time.monotonic()
    except KeyboardInterrupt:
        report.data["stopped_by_user"] = True
    except CheckFailure as error:
        failure = str(error)
    except (serial.SerialException, OSError):
        failure = "Serial or report access failed; raw output was suppressed."
    finally:
        try:
            device.connection.close()
        except (serial.SerialException, OSError):
            failure = failure or "Serial close failed."
        report.data.update(finished=True, elapsed_ms=int((time.monotonic() - started) * 1000), failure=failure)
        report.save()
    return failure is None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=120, help="Observe for 1 to 3600 seconds (default: 120)")
    parser.add_argument("--report", type=Path, help="JSON report path; partial metadata is saved throughout observation")
    args = parser.parse_args(argv)
    if not 1 <= args.seconds <= 3600:
        parser.error("--seconds must be between 1 and 3600.")
    report_root = Path(os.environ["LOCALAPPDATA"]) / "ESP32_Companion/qa" if os.environ.get("LOCALAPPDATA") else Path(__file__).resolve().parent.parent / "logs"
    report_path = args.report or report_root / (
        "robot-live-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S_%fZ") + ".json")
    if report_path.exists():
        parser.error("Choose a new report path to preserve existing observations.")
    try:
        ok = observe(args.seconds, LiveReport(report_path))
    except OSError:
        print("Live observer could not save its report; check file access or sync interference.", file=sys.stderr)
        return 1
    print("Live observation saved. Speech quality requires human confirmation." if ok else "Live observation stopped; partial metadata saved.")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
