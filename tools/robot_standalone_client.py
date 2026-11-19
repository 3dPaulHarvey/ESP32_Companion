"""Status-only observer for a robot that owns Wi-Fi, wake and conversations."""
import re
import time

from check_esp32_microphone import MicrophoneDevice
from microphone_calibration_robot import TrialCancelled, CheckFailure
from robot_conversation_client import ConversationDevice, conversation_metadata, RESULT
from robot_wake_client import TRIGGER, wake_metadata

FEATURE = r"Standalone: FEATURE version=1 boot_wifi=1 auto_wake=1 max_turns=2 pc_heartbeat=0 control_lease_ms=10000 failure_cooldown_ms=30000"
STATUS = r"Standalone: STATUS paused=([01]) wifi_wanted=([01]) cooldown_ms=(\d+) admission_open=([01])"
AUTO_TRIGGER = TRIGGER.replace("CloudWake:", "Standalone:")


def standalone_metadata(text, baseline, microphone):
    if "CloudWake: TRIGGER" in text or "CompanionConversation: OWNER mode=desktop" in text:
        raise CheckFailure("A manually armed turn cannot establish standalone operation.")
    if text.count("CompanionConversation: OWNER mode=standalone heartbeat_required=0") != 1:
        raise CheckFailure("The conversation did not run without the PC owner heartbeat.")
    if len(list(re.finditer(AUTO_TRIGGER, text))) != 1:
        raise CheckFailure("Expected one autonomous physical wake for this recorded trial.")
    proof = wake_metadata(text.replace("Standalone: TRIGGER", "CloudWake: TRIGGER"), baseline, microphone)
    proof.pop("single_turn")
    return {**proof, "autonomous": True, "pc_heartbeat_required": False}


class StandaloneDevice(ConversationDevice):
    def __init__(self, callback, cancel, closing):
        self.observer_callback = callback
        super().__init__(self.emit, cancel, closing)
        self.trigger_pattern = AUTO_TRIGGER
        self.status_commands = 0

    def emit(self, name, data):
        self.observer_callback(name, data)
        if name == "ready": self.observing_capture = False

    def send(self, data):
        if data != b"s":
            raise CheckFailure("The standalone observer can only query status; it cannot arm, pause, cancel or change Wi-Fi.")
        self.status_commands += 1
        return MicrophoneDevice.send(self, data)

    def keepalive(self):
        pass  # Deliberately send no control/owner heartbeat.

    def read(self):
        super().read()
        if self.observing_capture:
            match = re.search(r"Standalone: WAKE_LOCAL reason=([a-z_]+)", self.text[self.job_start:])
            if match: raise CheckFailure("The robot kept this wake local. Wait for its Wi-Fi/READY state before recording another trial.")


class StandaloneObserver:
    def __init__(self, callback, cancel=None, closing=None):
        import threading
        self.cancel = cancel or threading.Event(); self.closing = closing or threading.Event()
        self.device = StandaloneDevice(callback, self.cancel, self.closing)

    def wait_ready(self, timeout=90):
        d = self.device; deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.cancel.is_set() or self.closing.is_set(): raise TrialCancelled()
            start = d.send(b"s")
            state = d.wait(STATUS, start, timeout=5)
            network = d.wait(r"HardwareDiagnostics: STATUS wakes=(\d+) WiFi=(on|off) connected=([01])", start)
            local = d.wait(r"LocalCompanion: STATUS state=(\w+) .*pending=([01])", start)
            voice = d.wait(r"CompanionVoice: STATUS active=([01]) speaking=([01]) wake_armed=([01]) playback_busy=([01])", start)
            session = d.wait(r"CompanionSession: PROTOCOL version=1 mac=[0-9a-f:]+ configured=([01]) busy=([01])", start)
            if session[1] != "1": raise CheckFailure("The standalone robot needs its saved device credential before this test.")
            if state.groups() == ("0", "1", "0", "1") and network.groups()[1:] == ("on", "1") and local.groups() == ("IDLE", "0") and voice.groups() == ("0", "0", "1", "0") and session[2] == "0":
                return int(network[1])
            time.sleep(.2)
        raise CheckFailure("The robot did not reach autonomous Wi-Fi/READY. Check its display and saved network, then reconnect the observer.")

    def connect(self):
        d = self.device; d.connection.open()
        configured, _ = d.protocol()  # Checks the development MAC before trusting the profile.
        if not configured or d.capture["samples"] != 64000 or d.capture["pcm_shift"] != 15:
            raise CheckFailure("COM26 must run the configured, calibrated standalone profile.")
        d.trusted = True
        start = d.send(b"s"); d.wait(FEATURE, start)
        self.wait_ready()
        d.callback("preflight", {"measurement": {"passed": True, "case": "autonomous status readiness", "read_only": True,
            "questions_sent": 0, "pc_heartbeat_sent": 0, "cloud_probe_sent": False}})
        return {**d.capture, "observer_only": True, "conversation_max_turns": 2, "followup_ms": 4000}

    def capture(self, kind="followup"):
        if kind not in {"quiet", "followup"}: raise CheckFailure("Select a supported standalone exercise.")
        d = self.device
        baseline = self.wait_ready()
        d.job_start = start = len(d.text)
        d.captures = []; d.after_turn = 0; d.exercise = kind; d.observing_capture = True
        try:
            if self.cancel.is_set() or self.closing.is_set(): raise TrialCancelled()
            d.callback("waiting_wake", {"observer_only": True})
            d.wait(AUTO_TRIGGER, start, timeout=180)
            d.wait(RESULT, start, timeout=155)
            ready = d.wait(r"CompanionConversation: UI state=READY wake_armed=1", start)
            text = d.text[start:start + ready.end()]
            expected = 1 if kind == "quiet" else 2
            result = conversation_metadata(text, expected)
            if len(d.captures) != expected + 1 or [c["after_turn"] for c in d.captures] != list(range(expected + 1)):
                raise CheckFailure("The autonomous capture sequence was incomplete.")
            proof = standalone_metadata(text, baseline, d.captures[0]["measurement"])
            return {**result, "captures": list(d.captures), "wake": proof,
                "observer": {"status_commands": d.status_commands, "arm_commands": 0, "heartbeat_commands": 0, "network_commands": 0}}
        finally: d.observing_capture = False

    def poll(self):
        d = self.device
        if d.connection.in_waiting:
            d.read()
            if len(d.text) > 100000: d.text = d.text[-20000:]

    def close(self):
        self.device.observing_capture = False
        self.device.connection.close()  # Do not cancel the robot or change its network.
