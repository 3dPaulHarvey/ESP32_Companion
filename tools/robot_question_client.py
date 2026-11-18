"""USB controller for a user-started robot-to-Gemini microphone question."""
import re
import time

from check_esp32_microphone import MicrophoneDevice, microphone_metadata
from check_esp32_voice import VOICE_RESULT, voice_metadata
from microphone_calibration_robot import CalibratorDevice, RobotClient, TrialCancelled, CheckFailure

QUESTION_RESULT = VOICE_RESULT.replace("CompanionVoice:", "CompanionQuestion:").replace("no_audio_upload=1", "no_audio_upload=0")
INPUT_RESULT = r"CompanionQuestion: INPUT bytes=(\d+) chunks=(\d+) stream_ended=([01]) sample_rate=16000 input_cleared=1"


def question_metadata(text):
    try:
        output = voice_metadata(text, QUESTION_RESULT)
    except CheckFailure as error:
        raise CheckFailure(str(error).replace("Voice test", "Question test")) from None
    match = re.search(INPUT_RESULT, text)
    if not match or tuple(map(int, match.groups())) != (128000, 100, 1):
        raise CheckFailure("The robot did not send exactly one complete four-second PCM input stream.")
    if text.count("CompanionSession: REQUEST_BEGIN kind=question") != 1 or text.count("CompanionGemini: CONNECT_BEGIN") != 1:
        raise CheckFailure("The question test opened an unexpected number of cloud requests.")
    return {"output": output, "input_bytes": 128000, "input_chunks": 100, "input_stream_ended": True, "input_cleared": True}


class QuestionDevice(CalibratorDevice):
    def send(self, data):
        if data not in {b"s", b"w", b"o", b"q", b"j", b"x", b"[", b"]", b"="}:
            raise CheckFailure("The question test cannot send provisioning or unrelated diagnostic commands.")
        if data != b"s" and not self.trusted:
            raise CheckFailure("Development board identity has not been verified.")
        return MicrophoneDevice.send(self, data)

    def read(self):
        super().read()
        if not self.observing_capture: return
        fresh = self.text[self.job_start:]
        for state in ["CONNECTING", "SPEAKING", "READY"]:
            if f"CompanionQuestion: UI state={state}" in fresh and state not in self.seen_states:
                self.seen_states.add(state)
                self.callback(state.lower(), {})
        # Complete lines can be split across USB reads; inspect the bounded
        # current job for these controlled failure markers as well.
        if "CompanionQuestion: CAPTURE_DISCARDED" in self.text[self.job_start:]:
            raise CheckFailure("The question was cancelled or could not start. Its microphone buffer was cleared; retry when ready.")
        if re.search(r"CompanionQuestion: REQUEST_REJECTED reason=[a-z_]+", self.text[self.job_start:]):
            raise CheckFailure("The robot rejected this question because it was busy or offline. Reconnect and try again.")


class QuestionClient(RobotClient):
    def __init__(self, callback, cancel=None, closing=None):
        super().__init__(callback, cancel, closing)
        self.device.connection.close()
        self.device = QuestionDevice(callback, self.cancel, self.closing)

    def connect(self):
        policy = super().connect(hold_wake=False)
        self.hold(False)  # Release any old app lease before the setup-only check.
        d = self.device
        start = d.send(b"s")
        d.wait(r"CompanionQuestion: FEATURE enabled=1 explicit_start=1 capture_samples=64000 audio_upload=(?:manual_only|explicit_or_armed_wake)", start)
        if policy["pcm_shift"] != 15:
            raise CheckFailure("The robot must run the calibrated question firmware (capture shift 15).")
        start = d.send(b"w")
        d.wait(r"HardwareDiagnostics: WIFI connected ip=", start, timeout=40)
        # Verify admission, fresh session validation and Gemini setup before
        # enabling Start. This sends no question or microphone audio.
        d.check_setup()
        d.callback("preflight", {"measurement": dict(d.checks[-1])})
        self.hold(True)
        return policy

    def capture(self, kind="question"):
        d = self.device
        self.cancel.clear()
        d.job_start = len(d.text)
        d.seen_states = set()
        d.observing_capture = True
        try:
            self.hold(False)
            start = d.send(b"q")
            d.wait(r"LocalCompanion: CAPTURE_BEGIN request=\d+ target_samples=64000 bytes=128000", start, timeout=10)
            capture = d.wait_local_capture(start, source="question")
            microphone = microphone_metadata(capture, d.text[start:], d.capture)
            d.callback("microphone", {"measurement": microphone})
            d.wait(QUESTION_RESULT, start, timeout=120)
            d.wait(INPUT_RESULT, start)
            result = question_metadata(d.text[start:])
            d.wait(r"CompanionQuestion: UI state=SPEAKING wake_armed=0", start)
            d.wait(r"CompanionQuestion: UI state=READY wake_armed=1", start)
            self.hold(True)
            return {**result, "microphone": microphone}
        except TrialCancelled:
            self.cancel_and_idle()
            if not self.closing.is_set(): self.hold(True)
            raise
        finally:
            d.observing_capture = False

    def cancel_and_idle(self):
        d = self.device
        previous = d.interruptible
        d.interruptible = False
        d.observing_capture = False
        self.cancel.clear()
        try:
            d.send(b"x")
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                start = d.send(b"s")
                local = d.wait(r"LocalCompanion: STATUS state=(\w+) .*pending=([01])", start, timeout=2)
                voice = d.wait(r"CompanionVoice: STATUS active=([01]) speaking=([01]) wake_armed=[01] playback_busy=([01])", start)
                session = d.wait(r"CompanionSession: PROTOCOL version=1 mac=[0-9a-f:]+ configured=[01] busy=([01])", start)
                if local[1] == "IDLE" and local[2] == "0" and (voice[1], voice[2], voice[3], session[1]) == ("0", "0", "0", "0"): return
                time.sleep(.1)
            raise CheckFailure("The robot did not finish cleanup. Reset it before another question.")
        finally:
            d.interruptible = previous

    def close(self):
        d = self.device
        d.interruptible = False
        try:
            if d.connection.is_open and d.trusted:
                self.cancel_and_idle()
                start = d.send(b"o")
                d.wait(r"HardwareDiagnostics: WIFI (?:off;|already off;)", start, timeout=20)
                self.hold(False)
        finally:
            d.connection.close(); d.held = False
