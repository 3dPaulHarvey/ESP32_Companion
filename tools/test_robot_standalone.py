"""Autonomous ownership, read-only observation, diagnostic leases and USB checklist."""
import json
import copy
from pathlib import Path
import tempfile
import threading
import time
import tkinter as tk
import unittest

from robot_standalone_client import StandaloneObserver, StandaloneDevice, standalone_metadata, FEATURE, CheckFailure, TrialCancelled
from robot_conversation_test import ConversationApp
from microphone_calibration_robot import CalibratorDevice, RobotClient
from test_robot_conversation import capture_trace, network_trace
from test_robot_wake import PHYSICAL, WakeSerial
from test_esp32_session_tool import EXPECTED_MAC

READY_STATUS = ("LocalCompanion: CAPTURE_POLICY samples=64000 bytes=128000 duration_ms=4000 sample_rate=16000 worker_timeout_ms=5500 pcm_shift=15\n"
    "HardwareDiagnostics: STATUS wakes=7 WiFi=on connected=1\n"
    "LocalCompanion: STATUS state=IDLE started=3 pending=0\n"
    "CompanionVoice: STATUS active=0 speaking=0 wake_armed=1 playback_busy=0\n"
    f"CompanionSession: PROTOCOL version=1 mac={EXPECTED_MAC} configured=1 busy=0\n" + FEATURE + "\n"
    "Standalone: STATUS paused=0 wifi_wanted=1 cooldown_ms=0 admission_open=1\n")


def autonomous_trace(turns=2):
    return (PHYSICAL.replace("CloudWake: TRIGGER", "Standalone: TRIGGER") + capture_trace(3, source="voice_question") +
        "CompanionConversation: OWNER mode=standalone heartbeat_required=0\n" + network_trace(turns))


class ObserverSerial(WakeSerial):
    def __init__(self): super().__init__(); self.after_status = None
    def open(self): self.is_open = True
    def write(self, data):
        self.writes.append(data)
        if data == b"s":
            self.chunks.append(READY_STATUS.encode())
            if self.after_status:
                trace, self.after_status = self.after_status, None
                self.chunks.extend([trace[:77].encode(), trace[77:401].encode(), trace[401:].encode()])


class StandaloneChecks(unittest.TestCase):
    def observer(self, callback=lambda *args: None):
        client = StandaloneObserver(callback); client.device.connection = ObserverSerial()
        return client

    def test_status_only_connect_two_turns_and_close(self):
        events = []; client = self.observer(lambda name, data: events.append((name, data)))
        self.assertTrue(client.connect()["observer_only"])
        client.device.connection.after_status = autonomous_trace()
        result = client.capture("followup")
        self.assertTrue(result["wake"]["autonomous"])
        self.assertFalse(result["wake"]["pc_heartbeat_required"])
        self.assertEqual(result["completed_turns"], 2)
        self.assertEqual([c["after_turn"] for c in result["captures"]], [0, 1, 2])
        self.assertEqual([d["expect_speech"] for n, d in events if n == "recording"], [True, True, False])
        client.close()
        self.assertTrue(all(c == b"s" for c in client.device.connection.writes))
        self.assertFalse(client.device.connection.is_open)
        self.assertEqual(result["observer"]["heartbeat_commands"], 0)

    def test_quiet_after_first_turn_never_uploads_second_input(self):
        client = self.observer(); client.connect(); client.device.connection.after_status = autonomous_trace(1)
        result = client.capture("quiet")
        self.assertEqual(result["completed_turns"], 1)
        self.assertEqual(result["quiet_discards"], 1)
        self.assertEqual(len(result["captures"]), 2)
        client.close()

    def test_observer_cannot_change_robot_even_if_trusted(self):
        d = StandaloneDevice(lambda *a: None, threading.Event(), threading.Event()); d.connection = ObserverSerial(); d.trusted = True
        for cmd in [b"a", b"c", b"q", b"f", b"w", b"o", b"p", b"y", b"x", b"=", b"[", b"]", b"~private"]:
            with self.subTest(cmd=cmd), self.assertRaises(CheckFailure): d.send(cmd)
        d.held = d.conversation_owned = d.standalone_paused = True; d.last_heartbeat = 0; d.keepalive()
        self.assertEqual(d.connection.writes, [])

    def test_wrong_identity_never_receives_control(self):
        client = self.observer()
        client.device.connection.chunks = [READY_STATUS.replace(EXPECTED_MAC, "44:bd:8d:ed:4e:4c").encode()]
        with self.assertRaises(CheckFailure): client.connect()
        client.close(); self.assertEqual(client.device.connection.writes, [b"s"])

    def test_reopening_after_old_partial_status_waits_for_complete_policy(self):
        client = self.observer()
        # Previous owner's policy/header has already left the USB buffer.
        client.device.connection.chunks = [f"CompanionSession: PROTOCOL version=1 mac={EXPECTED_MAC} configured=1 busy=0\n".encode()]
        self.assertTrue(client.connect()["observer_only"])
        self.assertEqual(client.device.capture["pcm_shift"], 15)
        self.assertEqual(client.device.capture["samples"], 64000)
        client.close()

    def test_stop_only_stops_observation(self):
        client = self.observer(); client.connect(); client.cancel.set()
        with self.assertRaises(TrialCancelled): client.capture()
        client.close(); self.assertTrue(all(c == b"s" for c in client.device.connection.writes))

    def test_manual_owner_and_missing_physical_trigger_rejected(self):
        text = autonomous_trace()
        for bad in [text.replace("mode=standalone heartbeat_required=0", "mode=desktop heartbeat_required=1"),
                text.replace("Standalone: TRIGGER", "CloudWake: TRIGGER"), text.replace("source=model", "source=serial"),
                text.replace("OWNER mode=standalone heartbeat_required=0", "OWNER mode=standalone heartbeat_required=1")]:
            with self.subTest(trace=bad), self.assertRaises(CheckFailure): standalone_metadata(bad, 7, {"request": 3})

    def test_local_wake_cannot_be_counted_as_cloud_trial(self):
        client = self.observer(); client.connect()
        client.device.connection.after_status = "Standalone: WAKE_LOCAL reason=offline\n"
        with self.assertRaises(CheckFailure): client.capture()
        client.close()

    def test_diagnostic_pause_heartbeat_resume_and_expiry(self):
        d = CalibratorDevice(lambda *a: None, threading.Event(), threading.Event()); d.connection = ObserverSerial(); d.trusted = True
        d.standalone_paused = True; d.last_heartbeat = 0; d.keepalive()
        self.assertEqual(d.connection.writes, [b"="])
        d.check_standalone_control("Standalone: CONTROL paused=1 lease_ms=10000")
        with self.assertRaises(CheckFailure): d.check_standalone_control("Standalone: CONTROL_RELEASE reason=lease_expired paused=0")
        self.assertFalse(d.standalone_paused); self.assertTrue(d.standalone_control_lost)
        # A client that has lost its lease must not turn off or cancel the autonomous robot on close.
        client = RobotClient(lambda *a: None); client.device = d; client.close()
        self.assertEqual(d.connection.writes, [b"="])
        self.assertFalse(d.connection.is_open)

    def test_tk_self_paced_three_trials_disconnect_and_saved_power_check(self):
        with tempfile.TemporaryDirectory(prefix="esp32-standalone-ui-") as temp:
            window = tk.Tk(); app = ConversationApp(window, standalone=True, demo=True, results=Path(temp)); errors = []
            window.report_callback_exception = lambda *args: errors.append(args[1])
            def until(condition, timeout=12):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    window.update()
                    if condition(): break
                    time.sleep(.01)
                else: self.fail("Standalone UI did not reach the expected state.")
                self.assertFalse(errors, errors)
            try:
                app.connect(); until(lambda: app.connected)
                self.assertEqual(str(app.unplug_button['state']), "disabled")
                for kind in ["quiet", "followup", "followup"]:
                    self.assertEqual(app.exercise.get(), kind)
                    app.start(); until(lambda: app.current["state"] == "awaiting_confirmation"); app.feedback(True)
                self.assertEqual(str(app.unplug_button['state']), "normal")
                app.begin_unplugged(); until(lambda: app.phase == "unplugged")
                self.assertIsNone(app.worker.client)
                self.assertFalse(app.connected)
                self.assertEqual(str(app.arm_button['state']), "disabled")
                app.finish_unplugged(True)
                self.assertIsNone(app.data["unplugged_check"]["human_confirmed"])
                for value in app.unplug_vars: value.set(True); app.save_unplugged_progress()
                window.update_idletasks()
                self.assertLessEqual(app.finish_button.winfo_rooty() + app.finish_button.winfo_height(), window.winfo_rooty() + window.winfo_height())
                self.assertLessEqual(app.unplug_save_button.winfo_rooty() + app.unplug_save_button.winfo_height(), app.finish_button.winfo_rooty())
                app.finish_unplugged(True)
                saved = json.loads((app.folder / 'session.json').read_text(encoding='utf-8'))
                self.assertTrue(saved["unplugged_check"]["observer_disconnected"])
                self.assertEqual(saved["unplugged_check"]["state"], "verified_by_user")
                self.assertEqual(saved["unplugged_check"]["checks"], [True, True, True])
                self.assertEqual(len(saved["attempts"]), 3)
                self.assertTrue(saved["demo"])
                app.finish(); until(lambda: json.loads((app.folder / 'session.json').read_text(encoding='utf-8'))['status'] == 'closed')
            finally:
                app.worker.closing.set(); app.worker.cancel.set(); app.worker.join(timeout=3)
                try: window.destroy()
                except tk.TclError: pass

    def test_tk_idle_usb_loss_preserves_completed_or_review_pending_trial(self):
        for state in ["verified_by_user", "user_reported_problem", "awaiting_confirmation"]:
            with self.subTest(state=state), tempfile.TemporaryDirectory(prefix="esp32-observer-usb-loss-") as temp:
                window = tk.Tk(); app = ConversationApp(window, standalone=True, demo=True, results=Path(temp))
                try:
                    app.current = {"exercise": "followup", "state": state, "ended_at": "2026-11-19T01:52:40Z",
                        "human_confirmed": True if state == "verified_by_user" else False if state == "user_reported_problem" else None,
                        "measurement": {"completed_turns": 2, "normal_close": True}}
                    app.data["attempts"].append(app.current); previous = copy.deepcopy(app.current)
                    app.events.put(("error", {"message": "Cannot access COM26."}, time.monotonic()))
                    app.process_events()
                    saved = json.loads((app.folder / "session.json").read_text(encoding="utf-8"))
                    self.assertEqual(saved["attempts"], [previous])
                    self.assertEqual(saved["connection_events"][0]["after_attempt"], 1)
                    self.assertEqual(saved["connection_events"][0]["message"], "Cannot access COM26.")
                    self.assertFalse(app.connected)
                finally:
                    app.worker.closing.set(); app.worker.cancel.set(); app.worker.join(timeout=3); window.destroy()

    def test_tk_error_during_active_trial_is_still_saved_as_failure(self):
        with tempfile.TemporaryDirectory(prefix="esp32-observer-active-error-") as temp:
            window = tk.Tk(); app = ConversationApp(window, standalone=True, demo=True, results=Path(temp))
            try:
                app.current = {"exercise": "followup", "state": "recording", "human_confirmed": None, "captures": []}
                app.data["attempts"].append(app.current)
                app.events.put(("error", {"message": "Conversation audio_send_failed."}, time.monotonic()))
                app.process_events()
                saved = json.loads((app.folder / "session.json").read_text(encoding="utf-8"))
                self.assertEqual(saved["attempts"][0]["state"], "error")
                self.assertEqual(saved["attempts"][0]["error"], "Conversation audio_send_failed.")
                self.assertIsNone(saved["attempts"][0]["human_confirmed"])
                self.assertNotIn("connection_events", saved)
            finally:
                app.worker.closing.set(); app.worker.cancel.set(); app.worker.join(timeout=3); window.destroy()


if __name__ == "__main__": unittest.main()
