"""One explicitly armed physical wake, microphone question and spoken reply."""
import re
import time

from check_esp32_microphone import MicrophoneDevice, microphone_metadata
from robot_question_client import (
    QuestionClient, QuestionDevice, QUESTION_RESULT, INPUT_RESULT,
    question_metadata, TrialCancelled, CheckFailure,
)

ARMED = r"CloudWake: CONTROL armed=1 baseline_wakes=(\d+) lease_ms=10000 window_ms=60000"
TRIGGER = r"CloudWake: TRIGGER cycle=(\d+) wake_count=(\d+) source=model"
FEATURE = r"CloudWake: FEATURE version=1 single_turn=1 lease_ms=10000 window_ms=60000 boot_armed=0"


def wake_metadata(text, baseline, microphone):
    triggers = list(re.finditer(TRIGGER, text))
    if len(triggers) != 1:
        raise CheckFailure("Expected exactly one recognized wake for this armed question.")
    cycle, count = map(int, triggers[0].groups())
    if cycle != microphone["request"] or count <= baseline:
        raise CheckFailure("The question did not belong to a new physical wake after arming.")
    if not re.search(r"HardwareDiagnostics: WAKE_DETECTED count=" + str(count) + r"\b", text):
        raise CheckFailure("The physical wake event was missing.")
    if not re.search(r"LocalCompanion: STATE IDLE -> WAKE_DETECTED cycle=" + str(cycle) + r" source=voice_question\b", text):
        raise CheckFailure("The question did not start from the physical wake path.")
    return {"cycle": cycle, "wake_count": count, "baseline_wakes": baseline,
        "trigger_source": "model", "physical_wake_verified": True, "single_turn": True}


class WakeDevice(QuestionDevice):
    def __init__(self, callback, cancel, closing):
        super().__init__(callback, cancel, closing)
        self.wake_waiting = False

    def send(self, data):
        # There is deliberately no manual q, synthetic f, provisioning or
        # local n command. Only the ESP wake model can start this question.
        if data not in {b"s", b"w", b"o", b"j", b"x", b"[", b"]", b"=", b"a", b"z"}:
            raise CheckFailure("The wake test cannot issue manual questions or unrelated commands.")
        if data != b"s" and not self.trusted:
            raise CheckFailure("Development board identity has not been verified.")
        return MicrophoneDevice.send(self, data)

    def keepalive(self):
        if (self.held or self.wake_waiting) and time.monotonic() - self.last_heartbeat >= 2:
            self.send(b"=")
            self.last_heartbeat = time.monotonic()

    def read(self):
        super().read()  # Existing credential/fault guards run before events.
        if not self.observing_capture: return
        text = self.text[self.job_start:]
        if "CloudWake: ARM_REJECTED" in text:
            raise CheckFailure("The robot could not arm while busy or offline. Reconnect and retry.")
        match = re.search(r"CloudWake: DISARMED reason=([a-z_]+)", text)
        if match:
            self.wake_waiting = False
            if match[1] == "window_timeout":
                raise CheckFailure("No wake was recognized within one minute. Reconnect and arm again when ready.")
            raise CheckFailure("Wake permission ended before a question started. Reconnect and arm again.")
        trigger = re.search(TRIGGER, text)
        if trigger and "wake_triggered" not in self.seen_states:
            self.wake_waiting = False
            self.seen_states.add("wake_triggered")
            self.callback("wake_triggered", {"cycle": int(trigger[1]), "wake_count": int(trigger[2])})


class WakeClient(QuestionClient):
    def __init__(self, callback, cancel=None, closing=None):
        super().__init__(callback, cancel, closing)
        self.device.connection.close()
        self.device = WakeDevice(callback, self.cancel, self.closing)

    def connect(self):
        policy = super().connect()
        d = self.device
        start = d.send(b"s")
        d.wait(FEATURE, start)
        d.wait(r"CloudWake: STATUS armed=0", start)
        return {**policy, "cloud_wake": "explicit_one_shot", "wake_window_ms": 60000}

    def capture(self, kind="wake_question"):
        d = self.device
        d.job_start = len(d.text)
        d.seen_states = set()
        d.observing_capture = True
        try:
            if self.cancel.is_set() or self.closing.is_set(): raise TrialCancelled()
            d.callback("waiting_wake", {"window_ms": 60000})
            # Atomic firmware handover from calibration hold to armed wake.
            # Releasing with ] first would leave a race for an unarmed wake.
            start = d.send(b"a")
            d.held = False; d.wake_waiting = True
            armed = d.wait(ARMED, start)
            baseline = int(armed[1])
            d.wait(TRIGGER, start, timeout=65)
            d.wait(r"LocalCompanion: CAPTURE_BEGIN request=\d+ target_samples=64000 bytes=128000", start, timeout=10)
            capture = d.wait_local_capture(start, source="voice_question")
            microphone = microphone_metadata(capture, d.text[start:], d.capture)
            proof = wake_metadata(d.text[start:], baseline, microphone)
            d.callback("microphone", {"measurement": microphone})
            d.wait(QUESTION_RESULT, start, timeout=120)
            d.wait(INPUT_RESULT, start)
            result = question_metadata(d.text[start:])
            d.wait(r"CompanionQuestion: UI state=SPEAKING wake_armed=0", start)
            d.wait(r"CompanionQuestion: UI state=READY wake_armed=1", start)
            self.hold(True)
            # Validate again after playback/cleanup: a second trigger/request
            # anywhere in this trial must not be reported as a pass.
            proof = wake_metadata(d.text[start:], baseline, microphone)
            return {**result, "microphone": microphone, "wake": proof}
        except TrialCancelled:
            self.cancel_and_idle()
            if not self.closing.is_set(): self.hold(True)
            raise
        finally:
            d.observing_capture = False
            d.wake_waiting = False

    def cancel_and_idle(self):
        d = self.device
        d.observing_capture = False
        d.wake_waiting = False
        if d.connection.is_open and d.trusted: d.send(b"z")
        super().cancel_and_idle()
