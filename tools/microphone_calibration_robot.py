"""Single-owner USB adapter for the self-paced calibrator. Never opens a network."""
import json
import re
import threading
import time

from check_esp32_microphone import MicrophoneDevice, CheckFailure


class TrialCancelled(Exception):
    pass


class CalibratorDevice(MicrophoneDevice):
    def __init__(self, callback, cancel, closing):
        super().__init__()
        self.callback, self.cancel, self.closing = callback, cancel, closing
        self.trusted = False
        self.held = False
        self.last_heartbeat = 0
        self.interruptible = True
        self.observing_capture = False
        self.line_tail = ""

    def send(self, data):
        if data not in {b"s", b"n", b"x", b"o", b"[", b"]", b"="}:
            raise CheckFailure("The calibrator cannot issue cloud or provisioning commands.")
        if data != b"s" and not self.trusted:
            raise CheckFailure("Development board identity has not been verified.")
        return super().send(data)

    def keepalive(self):
        if self.held and time.monotonic() - self.last_heartbeat >= 2:
            self.send(b"=")
            self.last_heartbeat = time.monotonic()

    def read(self):
        if self.interruptible and (self.cancel.is_set() or self.closing.is_set()):
            raise TrialCancelled()
        self.keepalive()
        previous = len(self.text)
        super().read()  # Fault/credential output is rejected before observation.
        self.line_tail += self.text[previous:]
        while "\n" in self.line_tail:
            line, self.line_tail = self.line_tail.split("\n", 1)
            if self.held and "MicrophonePrep: CALIBRATOR_RELEASE reason=lease_expired" in line:
                self.held = False
                raise CheckFailure("Calibration control expired, for example after PC sleep. Reconnect before taking another sample.")
            if not self.observing_capture: continue
            if re.search(r"LocalCompanion: CAPTURE_BEGIN request=\d+", line):
                self.callback("preparing", {})
            elif re.search(r"MicrophonePrep: RECORDING request=\d+ duration_ms=4000", line):
                self.callback("recording", {"duration_ms": 4000})
            elif re.search(r"LocalCompanion: CAPTURE_COMPLETE request=\d+", line):
                self.callback("capture_end", {})
            elif re.search(r"HardwareDiagnostics: WAKE_DETECTED count=\d+", line):
                self.callback("wake_detected", {})
        if len(self.line_tail) > 8192:
            raise CheckFailure("Unexpected serial message size; contents suppressed.")

    def passed(self, label, **metadata):
        self.checks.append({"case": label, "passed": True, **metadata})


class RobotClient:
    def __init__(self, callback, cancel=None, closing=None):
        self.cancel = cancel or threading.Event()
        self.closing = closing or threading.Event()
        self.device = CalibratorDevice(callback, self.cancel, self.closing)

    def connect(self, *, hold_wake=True):
        device = self.device
        device.connection.open()
        configured, busy = device.protocol()
        if not configured or busy or device.capture["samples"] != 64000:
            raise CheckFailure("The development robot must run its four-second microphone firmware and be idle.")
        device.trusted = True
        start = device.send(b"s")
        device.wait(r"MicrophonePrep: CALIBRATOR_PROTOCOL version=1 lease_ms=10000 recording_event=1", start, timeout=5)
        device.wait(r"LocalCompanion: STATUS state=IDLE .*capture=local-4s pending=0", start, timeout=5)
        status = device.wait(r"HardwareDiagnostics: STATUS wakes=\d+ WiFi=([a-z]+) connected=([01])", start)
        if status[1] == "on":
            start = device.send(b"o")
            device.wait(r"HardwareDiagnostics: WIFI off; wake remains local;", start)
        if hold_wake: self.hold(True)
        return dict(device.capture)

    def hold(self, enabled):
        device = self.device
        start = device.send(b"[" if enabled else b"]")
        device.wait(r"MicrophonePrep: CALIBRATOR_CONTROL hold=" + ("1 lease_ms=10000" if enabled else "0 wake_armed=1"), start, timeout=4)
        device.held = enabled
        device.last_heartbeat = time.monotonic()

    def poll(self):
        device = self.device
        device.keepalive()
        if device.connection.in_waiting:
            device.read()
            if len(device.text) > 100000: device.text = device.text[-20000:]

    def capture(self, kind):
        device = self.device
        self.cancel.clear()
        device.observing_capture = True
        try:
            if kind == "wake":
                self.hold(False)
                device.callback("waiting_wake", {})
            result = device.capture_microphone(voice_wake=kind == "wake")
            if not device.held: self.hold(True)
            return result
        except TrialCancelled:
            self.cancel_and_idle()
            if not self.closing.is_set() and not device.held: self.hold(True)
            raise
        finally:
            device.observing_capture = False

    def cancel_and_idle(self):
        device = self.device
        previous = device.interruptible
        device.interruptible = False
        try:
            self.cancel.clear()
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                start = device.send(b"s")
                status = device.wait(r"LocalCompanion: STATUS state=(\w+) .*capture=local-4s pending=([01])", start, timeout=.8)
                if status[1] == "IDLE" and status[2] == "0": return
                if status[1] == "LISTENING": device.send(b"x")
                time.sleep(.05)
            raise CheckFailure("The robot has not finished capture cleanup; its safety lease will restore wake.")
        finally:
            device.interruptible = previous

    def close(self):
        device = self.device
        device.interruptible = False
        try:
            if device.connection.is_open and device.trusted:
                self.cancel_and_idle()
                self.hold(False)
        finally:
            device.connection.close()
            device.held = False


class DemoRobot:
    """UI preview data is isolated from real user calibration sessions."""
    def __init__(self, callback, cancel=None, closing=None):
        self.callback = callback
        self.cancel = cancel or threading.Event()
        self.closing = closing or threading.Event()

    def connect(self):
        return {"samples": 64000, "bytes": 128000, "duration_ms": 4000, "worker_timeout_ms": 5500, "pcm_shift": 14}

    def poll(self):
        pass

    def capture(self, kind):
        self.cancel.clear()
        if kind == "wake": self.callback("waiting_wake", {})
        self.callback("preparing", {})
        time.sleep(.2)
        self.callback("recording", {"duration_ms": 4000})
        for _ in range(80):
            if self.cancel.is_set() or self.closing.is_set(): raise TrialCancelled()
            time.sleep(.05)
        self.callback("capture_end", {})
        level, peak = (180, 900) if kind == "quiet" else (2300, 12000)
        return {"request": 1, "samples": 64000, "bytes": 128000, "duration_ms": 4000, "elapsed_ms": 4200,
            "rms": level, "peak": peak, "clipped": 0, "pcm_shift": 14, "raw_peak": peak << 14,
            "legacy_clipped": 0 if kind == "quiet" else 12, "raw_near_full_scale": 0,
            "suggested_shift": 12 if kind == "quiet" else 14, "dc_mean": 0, "ac_rms": level,
            "windows": [{"request": 1, "second": i, "samples": 16000, "rms": level, "peak": peak, "clipped": 0} for i in range(1, 5)]}

    def cancel_and_idle(self):
        self.cancel.clear()

    def close(self):
        pass
