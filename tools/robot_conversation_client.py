"""Explicitly armed, bounded two-turn robot conversation; numeric evidence only."""
import re
import time

from check_esp32_microphone import MicrophoneDevice, microphone_metadata
from microphone_calibration_robot import CalibratorDevice, TrialCancelled, CheckFailure
from robot_question_client import QuestionClient
from robot_wake_client import ARMED, TRIGGER, wake_metadata

FEATURE = r"CompanionConversation: FEATURE version=1 arm=c max_turns=2 followup_ms=4000 owner_lease_ms=10000 boot_armed=0"
RESULT = (r"CompanionConversation: RESULT ok=([01]) http=(\d+) reason=([a-z_]+) upgrade_http=(\d+) setup_ack=([01]) "
    r"close_sent=([01]) close_ack=([01]) close_code=(\d+) turns=(\d+) followup_captures=(\d+) quiet_discards=(\d+) elapsed_ms=(\d+) cleanup=1 same_connection=1 fixture=([01])")
TURN = (r"CompanionConversation: TURN turn=(\d+) complete=1 audio_bytes=(\d+) chunks=(\d+) sample_rate=24000 "
    r"played_samples=(\d+) duration_ms=(\d+) rms=(\d+) peak=(\d+) clipped=(\d+) playback_ms=(\d+)")
INPUT = r"CompanionConversation: INPUT turn=(\d+) bytes=(\d+) chunks=(\d+) stream_ended=1 input_cleared=1"
VAD = r"CompanionConversation: VAD after_turn=(\d+) frames=(\d+) speech_frames=(\d+) longest=(\d+) accepted=([01]) mode=3"
CAPTURE = (r"LocalCompanion: CAPTURE_COMPLETE request=(\d+) samples=(\d+) bytes=(\d+) duration_ms=(\d+) "
    r"sample_rate=16000 elapsed_ms=(\d+) rms=(\d+) peak=(\d+) clipped=(\d+)")
UPLOAD_HEALTH = (r"CompanionConversation: UPLOAD_HEALTH turn=(\d+) bytes=(\d+) chunks=(\d+) ended=([01]) "
    r"upload_ms=(\d+) max_send_ms=(\d+) slow_sends=(\d+)")
SEND_FAILURE = (r"CompanionConversation: SEND_FAILURE turn=(\d+) opcode=(\d+) expected=(\d+) returned=(-?\d+) elapsed_ms=(\d+) "
    r"system_errno=(-?\d+) socket_errno=(-?\d+) tls_status=(-?\d+) tls_error=(-?\d+) tls_flags=(-?\d+) "
    r"input_bytes=(\d+) input_chunks=(\d+) wifi=([01]) rssi=(-?\d+) internal_free=(\d+)")
WRITE_FAILURE = r"CompanionConversation: WRITE_FAILURE status=(complete|timeout|stopped|fatal|invalid) written=(\d+) calls=(\d+) waits=(\d+) partials=(\d+) last_return=(-?\d+)"
WRITE_STATS = r"CompanionConversation: WRITE_STATS calls=(\d+) waits=(\d+) partials=(\d+) question_retries=0"
CONNECTION_TIMING = r"CompanionConversation: CONNECTION_TIMING upgrade_ms=(\d+) ready_ms=(\d+)"
TURN_TIMING = (r"CompanionConversation: TIMING turn=(\d+) upload_ms=(\d+) first_audio_ms=(\d+) "
    r"generation_complete_ms=(\d+) turn_complete_ms=(\d+) playback_start_ms=(\d+)")
PLAYBACK_BEGIN = (r"CompanionConversation: PLAYBACK_BEGIN turn=(\d+) since_upload_ms=(\d+) "
    r"generation_complete=([01]) turn_complete=([01]) mono_us=(\d+)")
SESSION_FETCH = (r"CompanionSession: FETCH (?:kind=([a-z_]+) )?elapsed_ms=(\d+) "
    r"(?:ok=([01]) )?http=(\d+) reason=([a-z_]+)")


def transport_observations(line):
    for event, pattern, names in [
            ("upload_health", UPLOAD_HEALTH, ["turn", "bytes", "chunks", "ended", "upload_ms", "max_send_ms", "slow_sends"]),
            ("transport_failure", SEND_FAILURE, ["turn", "opcode", "expected", "returned", "elapsed_ms", "system_errno", "socket_errno",
                "tls_status", "tls_error", "tls_flags", "input_bytes", "input_chunks", "wifi", "rssi", "internal_free"]),
            ("connection_timing", CONNECTION_TIMING, ["upgrade_ms", "ready_ms"]),
            ("turn_timing", TURN_TIMING, ["turn", "upload_ms", "first_audio_ms", "generation_complete_ms", "turn_complete_ms", "playback_start_ms"]),
            ("playback_begin", PLAYBACK_BEGIN, ["turn", "since_upload_ms", "generation_complete", "turn_complete", "mono_us"]),
            ("write_stats", WRITE_STATS, ["calls", "waits", "partials"])]:
        match = re.search(pattern, line)
        if match: return event, dict(zip(names, map(int, match.groups())))
    match = re.search(SESSION_FETCH, line)
    if match:
        data = {"elapsed_ms": int(match[2]), "http": int(match[4]), "reason": match[5]}
        if match[1] is not None: data["kind"] = match[1]
        if match[3] is not None: data["ok"] = int(match[3])
        return "session_fetch", data
    match = re.search(WRITE_FAILURE, line)
    if match:
        return "write_failure", {"status": match[1], **dict(zip(["written", "calls", "waits", "partials", "last_return"], map(int, match.groups()[1:])))}
    match = re.search(r"CompanionConversation: CLEANUP reason=([a-z_]+)", line)
    if match: return "transport_cleanup", {"reason": match[1]}
    return None


def conversation_metadata(text, expected_turns, *, fixture=False):
    matches = list(re.finditer(RESULT, text))
    if len(matches) != 1: raise CheckFailure("Expected one complete conversation result.")
    m = matches[0]
    numeric = [int(m[i]) for i in range(1, 14) if i != 3]
    ok, http, upgrade, setup, close_sent, close_ack, close_code, turns, captures, quiet, elapsed, is_fixture = numeric
    if (ok, http, upgrade, setup, close_sent, close_ack, close_code, is_fixture) != (1, 200, 101, 1, 1, 1, 1000, int(fixture)):
        raise CheckFailure(f"Conversation did not finish cleanly (HTTP {http}, upgrade {upgrade}, reason {m[3]}).")
    if m[3] != "quiet_timeout" or (turns, captures, quiet) != (expected_turns, expected_turns, 1):
        raise CheckFailure("The expected follow-up speech/silence sequence did not complete. The saved levels can help us adjust it.")
    kind = "conversation_fixture" if fixture else "conversation"
    if text.count(f"CompanionSession: REQUEST_BEGIN kind={kind} ") != 1 or text.count("CompanionSession: REQUEST_BEGIN") != 1 or text.count("CompanionGemini: CONNECT_BEGIN") != 1:
        raise CheckFailure("Expected one session request and one Gemini connection for the whole conversation.")
    inputs = [tuple(map(int, item.groups())) for item in re.finditer(INPUT, text)]
    if inputs != [(i, 128000, 100) for i in range(1, expected_turns + 1)]:
        raise CheckFailure("Each spoken turn must upload exactly one complete four-second input and EOF.")
    outputs = []
    for item in re.finditer(TURN, text):
        out = dict(zip(["turn", "audio_bytes", "chunks", "played_samples", "duration_ms", "rms", "peak", "clipped", "playback_ms"], map(int, item.groups())))
        if (not 0 < out["audio_bytes"] <= 480000 or out["audio_bytes"] != out["played_samples"] * 2 or
                out["duration_ms"] != out["played_samples"] * 1000 // 24000 or not 0 < out["chunks"] <= 256 or
                not 0 <= out["rms"] <= out["peak"] <= 32768 or not 0 <= out["clipped"] <= out["played_samples"] or
                not out["duration_ms"] <= out["playback_ms"] <= out["duration_ms"] + 2500):
            raise CheckFailure("Reply playback did not match its bounded audio/sample metrics.")
        outputs.append(out)
    if [out["turn"] for out in outputs] != list(range(1, expected_turns + 1)):
        raise CheckFailure("Missing or duplicate spoken reply result.")
    decisions = [dict(zip(["after_turn", "frames", "speech_frames", "longest", "accepted"], map(int, item.groups()))) for item in re.finditer(VAD, text)]
    if [v["after_turn"] for v in decisions] != list(range(1, expected_turns + 1)):
        raise CheckFailure("Missing or duplicate follow-up speech decision.")
    for decision in decisions:
        if (decision["frames"] != 200 or not 0 <= decision["longest"] <= decision["speech_frames"] <= 200 or
                bool(decision["accepted"]) != (decision["speech_frames"] >= 10 and decision["longest"] >= 5)):
            raise CheckFailure("Local speech-decision metrics failed validation.")
    if [v["accepted"] for v in decisions] != ([1, 0] if expected_turns == 2 else [0]):
        raise CheckFailure("The follow-up did not distinguish speech from the requested silence window.")
    if not 0 < elapsed < 160000: raise CheckFailure("Conversation exceeded its bounded session time.")
    timing = {}
    for line in text.splitlines():
        observation = transport_observations(line)
        if not observation: continue
        event, data = observation
        if event == "connection_timing": timing[event] = data
        elif event in {"turn_timing", "playback_begin", "session_fetch"}: timing.setdefault(event, []).append(data)
    return {"completed_turns": turns, "same_connection": True, "normal_close": True, "quiet_timeout": True,
        "quiet_discards": quiet, "input_bytes_per_turn": 128000, "outputs": outputs, "vad": decisions, "elapsed_ms": elapsed, "fixture": fixture,
        **timing}


class ConversationDevice(CalibratorDevice):
    def __init__(self, callback, cancel, closing):
        super().__init__(callback, cancel, closing)
        self.conversation_owned = False; self.wake_waiting = False
        self.job_start = 0; self.after_turn = 0; self.exercise = "followup"; self.captures = []
        self.trigger_pattern = TRIGGER

    def send(self, data):
        if data not in {b"s", b"w", b"o", b"j", b"x", b"[", b"]", b"=", b"c", b"z", b"p", b"y"}:
            raise CheckFailure("The conversation tester cannot send manual questions, fixtures or provisioning commands.")
        if data != b"s" and not self.trusted: raise CheckFailure("Development board identity has not been verified.")
        return MicrophoneDevice.send(self, data)

    def keepalive(self):
        if (self.held or self.conversation_owned or self.standalone_paused) and time.monotonic() - self.last_heartbeat >= 2:
            self.send(b"="); self.last_heartbeat = time.monotonic()

    def read(self):
        if self.interruptible and (self.closing.is_set() or (self.observing_capture and self.cancel.is_set())): raise TrialCancelled()
        self.keepalive(); previous = len(self.text)
        MicrophoneDevice.read(self)  # Privacy/fault checks precede every event.
        self.line_tail += self.text[previous:]
        while "\n" in self.line_tail:
            line, self.line_tail = self.line_tail.split("\n", 1)
            self.check_standalone_control(line)
            if self.held and "CALIBRATOR_RELEASE reason=lease_expired" in line:
                self.held = False; raise CheckFailure("Test control expired. Reconnect before another conversation.")
            if not self.observing_capture: continue
            observation = transport_observations(line)
            if observation: self.callback(*observation)
            if "CloudWake: ARM_REJECTED" in line or "CloudWake: DISARMED reason=" in line:
                self.wake_waiting = False
                raise CheckFailure("The waiting arm ended or was rejected. Reconnect and arm when ready.")
            if "CompanionConversation: OWNER_EXPIRED" in line:
                raise CheckFailure("Conversation control expired; the robot is cancelling. Reconnect when ready.")
            if "CompanionQuestion: REQUEST_REJECTED" in line or "CompanionQuestion: CAPTURE_DISCARDED" in line:
                raise CheckFailure("The robot could not start this question; its input was cleared.")
            m = re.search(self.trigger_pattern, line)
            if m:
                self.wake_waiting = False
                self.callback("wake_triggered", {"cycle": int(m[1]), "wake_count": int(m[2])})
            m = re.search(r"CompanionConversation: UI state=FOLLOWUP after_turn=(\d+)", line)
            if m:
                self.after_turn = int(m[1]); self.callback("followup_window", {"after_turn": self.after_turn})
            m = re.search(r"CompanionConversation: UI state=(CONNECTING|SPEAKING) turn=(\d+)", line)
            if m: self.callback(m[1].lower(), {"turn": int(m[2])})
            if "CompanionConversation: UI state=READY" in line: self.callback("ready", {})
            if re.search(r"MicrophonePrep: RECORDING request=\d+ duration_ms=4000", line):
                self.callback("recording", {"duration_ms": 4000, "after_turn": self.after_turn,
                    "expect_speech": self.after_turn == 0 or (self.exercise == "followup" and self.after_turn == 1)})
            if re.search(CAPTURE, line): self.callback("capture_end", {"after_turn": self.after_turn})
            m = re.search(r"LocalCompanion: CYCLE_COMPLETE cycle=(\d+) .*source=(\w+)", line)
            if m:
                request = int(m[1]); expected_source = "voice_question" if not self.captures else "followup_question"
                if m[2] != expected_source: raise CheckFailure("Unexpected capture source during the armed conversation.")
                found = [v for v in re.finditer(CAPTURE, self.text[self.job_start:]) if int(v[1]) == request]
                if len(found) != 1: raise CheckFailure("Missing or duplicate microphone result for this cycle.")
                cap = dict(zip(["request", "samples", "bytes", "duration_ms", "elapsed_ms", "rms", "peak", "clipped"], map(int, found[0].groups())))
                if any(cap[k] != self.capture[k] for k in ["samples", "bytes", "duration_ms"]) or not 3800 <= cap["elapsed_ms"] < 5500:
                    raise CheckFailure("Microphone capture did not match the calibrated four-second policy.")
                if not 0 <= cap["rms"] <= cap["peak"] <= 32768 or not 0 <= cap["clipped"] <= cap["samples"]:
                    raise CheckFailure("Invalid bounded microphone levels.")
                measurement = microphone_metadata(cap, self.text[self.job_start:], self.capture)
                saved = {"after_turn": self.after_turn, "measurement": measurement}
                self.captures.append(saved); self.callback("microphone", saved)
            m = re.search(TURN, line)
            if m: self.callback("turn_result", {"turn": int(m[1]), "measurement": dict(zip(
                ["audio_bytes", "chunks", "played_samples", "duration_ms", "rms", "peak", "clipped", "playback_ms"], map(int, m.groups()[1:])))})
            m = re.search(VAD, line)
            if m: self.callback("vad", dict(zip(["after_turn", "frames", "speech_frames", "longest", "accepted"], map(int, m.groups()))))
        if len(self.line_tail) > 8192: raise CheckFailure("Unexpected serial message size; contents suppressed.")


class ConversationClient(QuestionClient):
    def __init__(self, callback, cancel=None, closing=None):
        super().__init__(callback, cancel, closing)
        self.device.connection.close()
        self.device = ConversationDevice(callback, self.cancel, self.closing)

    def connect(self):
        policy = super().connect()
        start = self.device.send(b"s"); self.device.wait(FEATURE, start)
        self.device.wait(r"CloudWake: STATUS armed=0", start)
        return {**policy, "conversation_max_turns": 2, "followup_ms": 4000}

    def capture(self, kind="followup"):
        if kind not in {"followup", "quiet"}: raise CheckFailure("Select a supported conversation exercise.")
        d = self.device; d.job_start = len(d.text); d.captures = []; d.after_turn = 0; d.exercise = kind
        d.observing_capture = True
        try:
            if self.cancel.is_set() or self.closing.is_set(): raise TrialCancelled()
            d.callback("waiting_wake", {"window_ms": 60000})
            start = d.send(b"c"); d.held = False; d.conversation_owned = d.wake_waiting = True
            armed = d.wait(ARMED, start)
            d.wait(TRIGGER, start, timeout=65)
            d.wait(RESULT, start, timeout=155)
            d.wait(r"CompanionConversation: UI state=READY wake_armed=1", start)
            self.hold(True)
            expected = 2 if kind == "followup" else 1
            result = conversation_metadata(d.text[start:], expected)
            if len(d.captures) != expected + 1 or [c["after_turn"] for c in d.captures] != list(range(expected + 1)):
                raise CheckFailure("Missing first question, follow-up or final quiet capture evidence.")
            proof = wake_metadata(d.text[start:], int(armed[1]), d.captures[0]["measurement"])
            proof.pop("single_turn")
            return {**result, "captures": list(d.captures), "wake": proof}
        except TrialCancelled:
            self.cancel_and_idle()
            if not self.closing.is_set(): self.hold(True)
            raise
        finally:
            d.observing_capture = False; d.conversation_owned = d.wake_waiting = False

    def cancel_and_idle(self):
        d = self.device; d.observing_capture = False; d.wake_waiting = False
        if d.connection.is_open and d.trusted: d.send(b"z")
        try: super().cancel_and_idle()
        finally: d.conversation_owned = False
