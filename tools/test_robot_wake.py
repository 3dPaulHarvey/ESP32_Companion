"""Physical-wake correlation, serial ownership and self-paced autosave checks."""
import json
from pathlib import Path
import tempfile
import threading
import time
import tkinter as tk
import unittest
from unittest import mock

from robot_wake_client import WakeClient, WakeDevice, wake_metadata, CheckFailure, TrialCancelled
from robot_question_test import QuestionApp, DemoWake
from test_robot_question import TRACE
from test_esp32_microphone_tool import CAPTURE, HEADROOM, WINDOWS
from test_esp32_session_tool import FakeSerial, EXPECTED_MAC

PHYSICAL = ("HardwareDiagnostics: WAKE_DETECTED count=8 WiFi=on\n"
    "LocalCompanion: STATE IDLE -> WAKE_DETECTED cycle=3 source=voice_question\n"
    "CloudWake: TRIGGER cycle=3 wake_count=8 source=model\n")
CAPTURE_TRACE = ("LocalCompanion: CAPTURE_BEGIN request=3 target_samples=64000 bytes=128000\n"
    "MicrophonePrep: RECORDING request=3 duration_ms=4000\n"
    "LocalCompanion: CAPTURE_COMPLETE request=3 samples=64000 bytes=128000 duration_ms=4000 sample_rate=16000 elapsed_ms=4120 rms=1000 peak=16384 clipped=0\n" +
    HEADROOM.replace("pcm_shift=14", "pcm_shift=15").replace("268435456", "536870912").replace("suggested_shift=14", "suggested_shift=15") + WINDOWS +
    "LocalCompanion: CYCLE_COMPLETE cycle=3 completed=3 source=voice_question voice_started=3 serial_started=0 aborted=0\n")
ARMED_TRACE = "CloudWake: CONTROL armed=1 baseline_wakes=7 lease_ms=10000 window_ms=60000\n"
REPLY_TRACE = (TRACE + "CompanionQuestion: UI state=SPEAKING wake_armed=0\n"
    "CompanionQuestion: UI state=READY wake_armed=1\n")


class WakeSerial(FakeSerial):
    def __init__(self, *, arm_response=None):
        super().__init__()
        self.is_open = True
        self.arm_response = arm_response

    def close(self): self.is_open = False

    def write(self, data):
        super().write(data)
        if data == b"a":
            self.chunks.extend([part.encode() for part in (
                [self.arm_response] if self.arm_response is not None else
                [ARMED_TRACE, PHYSICAL[:90], PHYSICAL[90:], CAPTURE_TRACE, REPLY_TRACE])])
        elif data == b"[": self.chunks.append(b"MicrophonePrep: CALIBRATOR_CONTROL hold=1 lease_ms=10000\n")
        elif data == b"]": self.chunks.append(b"MicrophonePrep: CALIBRATOR_CONTROL hold=0 wake_armed=1\n")
        elif data == b"z": self.chunks.append(b"CloudWake: CONTROL armed=0\n")
        elif data == b"o": self.chunks.append(b"HardwareDiagnostics: WIFI off; wake remains local;\n")
        elif data == b"s": self.chunks.append((
            "LocalCompanion: STATUS state=IDLE started=3 pending=0\n"
            "CompanionVoice: STATUS active=0 speaking=0 wake_armed=1 playback_busy=0\n"
            f"CompanionSession: PROTOCOL version=1 mac={EXPECTED_MAC} configured=1 busy=0\n").encode())


def fake_client(callback=lambda *args: None, **serial_options):
    client = WakeClient(callback)
    client.device.connection = WakeSerial(**serial_options)
    client.device.trusted = True
    client.device.held = True
    client.device.last_heartbeat = time.monotonic()
    client.device.capture = {"samples": 64000, "bytes": 128000, "duration_ms": 4000, "worker_timeout_ms": 5500, "pcm_shift": 15}
    return client


class WakeChecks(unittest.TestCase):
    def test_one_physical_wake_full_upload_reply_and_hold(self):
        events = []
        client = fake_client(lambda name, data: events.append(name))
        result = client.capture()
        self.assertTrue(result["wake"]["physical_wake_verified"])
        self.assertEqual(result["wake"]["baseline_wakes"], 7)
        self.assertEqual(result["input_bytes"], 128000)
        self.assertEqual(client.device.connection.writes, [b"a", b"["])
        self.assertEqual(events.count("wake_triggered"), 1)
        self.assertLess(events.index("waiting_wake"), events.index("recording"))
        self.assertTrue(client.device.held)
        self.assertFalse(client.device.wake_waiting)
        client.close()
        self.assertIn(b"z", client.device.connection.writes)
        self.assertIn(b"o", client.device.connection.writes)
        self.assertFalse(client.device.connection.is_open)

    def test_manual_or_uncorrelated_capture_cannot_pass(self):
        for text in [PHYSICAL.replace("cycle=3", "cycle=2"),
                PHYSICAL.replace("wake_count=8", "wake_count=7"),
                PHYSICAL.replace("source=voice_question", "source=question"),
                PHYSICAL.replace("WAKE_DETECTED count=8", "WAKE_DETECTED count=9"),
                PHYSICAL + PHYSICAL, ""]:
            with self.subTest(trace=text), self.assertRaises(CheckFailure): wake_metadata(text, 7, CAPTURE)

    def test_no_manual_question_or_provisioning_commands(self):
        d = WakeDevice(lambda *args: None, threading.Event(), threading.Event())
        d.connection = FakeSerial()
        for command in [b"a", b"j", b"z"]:
            with self.assertRaises(CheckFailure): d.send(command)
        d.trusted = True
        for command in [b"q", b"f", b"n", b"v", b"~credentials", b"aa"]:
            with self.assertRaises(CheckFailure): d.send(command)
        self.assertEqual(d.connection.writes, [])

    def test_only_waiting_or_held_owner_sends_heartbeats(self):
        client = fake_client(); d = client.device; d.held = False; d.last_heartbeat = 0
        d.keepalive(); self.assertEqual(d.connection.writes, [])
        d.wake_waiting = True; d.keepalive()
        self.assertEqual(d.connection.writes, [b"="])
        d.wake_waiting = False; d.last_heartbeat = 0; d.keepalive()
        self.assertEqual(d.connection.writes, [b"="])

    def test_rejected_and_expired_arms_fail_without_a_question(self):
        for response in ["CloudWake: ARM_REJECTED reason=control_busy_or_offline\n",
                ARMED_TRACE + "CloudWake: DISARMED reason=window_timeout\n",
                ARMED_TRACE + "CloudWake: DISARMED reason=lease_expired\n"]:
            client = fake_client(arm_response=response)
            with self.subTest(response=response), self.assertRaises(CheckFailure): client.capture()
            self.assertFalse(client.device.wake_waiting)
            self.assertNotIn(b"q", client.device.connection.writes)

    def test_stop_already_requested_does_not_arm(self):
        client = fake_client(); client.cancel.set()
        with self.assertRaises(TrialCancelled): client.capture()
        self.assertNotIn(b"a", client.device.connection.writes)
        self.assertIn(b"z", client.device.connection.writes)
        self.assertTrue(client.device.held)

    def test_sensitive_transport_output_still_suppressed(self):
        client = fake_client()
        client.device.connection.chunks = [b"transport ?access_token=private-fixture\n"]
        with self.assertRaises(CheckFailure) as error: client.device.read()
        self.assertNotIn("private-fixture", str(error.exception))

    def test_wait_stop_retry_answer_and_close_are_autosaved(self):
        # Keep disposable UI fixtures outside the synced project directory.
        with tempfile.TemporaryDirectory(prefix="esp32-wake-ui-") as temp:
            window = tk.Tk(); app = QuestionApp(window, demo=True, results=Path(temp), wake=True)
            errors = []
            window.report_callback_exception = lambda *args: errors.append(args[1])
            def until(condition, timeout=8):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    window.update()
                    if condition(): break
                    time.sleep(.01)
                else: self.fail("Wake tester did not reach the expected state.")
                self.assertFalse(errors, errors)
            try:
                self.assertEqual(app.worker.client_factory, DemoWake)
                self.assertEqual(str(app.start_button["text"]), "Arm next wake")
                app.connect(); until(lambda: app.connected)
                self.assertEqual(app.data["attempts"], [])
                app.start(); until(lambda: app.status.get() == "Say Hi, E-S-P")
                self.assertIsNone(app.countdown_job)
                self.assertEqual(app.current["state"], "waiting_wake")
                app.stop(); until(lambda: not app.busy)
                self.assertEqual(app.current["state"], "cancelled")
                app.start(); until(lambda: app.current["state"] == "awaiting_confirmation")
                self.assertEqual(app.current["wake_trigger"]["wake_count"], 1)
                self.assertTrue(app.current["measurement"]["wake"]["demo"])
                app.feedback(True)
                saved = json.loads((app.folder / "session.json").read_text(encoding="utf-8"))
                self.assertTrue(saved["demo"])
                self.assertTrue(saved["physical_wake_required"])
                self.assertTrue(saved["attempts"][-1]["human_confirmed"])
                self.assertFalse(saved["raw_audio_saved"])
                self.assertFalse(saved["transcript_saved"])
                window.update_idletasks()
                self.assertLessEqual(app.finish_button.winfo_rooty() + app.finish_button.winfo_height(), window.winfo_rooty() + window.winfo_height())
                app.start(); until(lambda: app.status.get() == "Say Hi, E-S-P")
                app.finish(); until(lambda: json.loads((app.folder / "session.json").read_text(encoding="utf-8"))["status"] == "closed")
                saved = json.loads((app.folder / "session.json").read_text(encoding="utf-8"))
                self.assertIn(saved["attempts"][-1]["state"], {"cancelled", "interrupted"})
            finally:
                app.worker.closing.set(); app.worker.cancel.set(); app.worker.join(timeout=3)
                try: window.destroy()
                except tk.TclError: pass


if __name__ == "__main__": unittest.main()
