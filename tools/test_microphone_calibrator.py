"""Persistence, review gates and USB ownership checks; no real audio or network."""
import copy
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest import mock

from microphone_calibration_core import AppInstance, CalibrationSession, atomic_json, clean_measurement
from microphone_calibration_robot import RobotClient, CalibratorDevice, CheckFailure
from test_esp32_session_tool import FakeSerial, EXPECTED_MAC
from test_esp32_microphone_tool import CAPTURE, HEADROOM, WINDOWS, POLICY
from check_esp32_microphone import microphone_metadata


def measurement(quiet=False):
    value = microphone_metadata(CAPTURE, HEADROOM + WINDOWS, POLICY)
    if quiet:
        value.update(rms=100, peak=500, ac_rms=100, raw_peak=500 << 14, legacy_clipped=0, suggested_shift=12)
        for window in value["windows"]: window.update(rms=100, peak=500)
    return value


class CalibrationChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="esp32-calibrator-unit-")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_launch_and_partial_close_are_saved(self):
        session = CalibrationSession(self.root)
        identity = session.start_trial()
        session.close()
        saved = json.loads(session.path.read_text(encoding="utf-8"))
        self.assertEqual(saved["status"], "partial")
        self.assertEqual(saved["trials"][0]["id"], identity)
        self.assertEqual(saved["trials"][0]["state"], "interrupted")
        self.assertFalse(saved["assessment"]["required_complete"])

    def test_second_app_cannot_overwrite_a_running_session(self):
        first = AppInstance(self.root)
        try:
            with self.assertRaises(RuntimeError): AppInstance(self.root)
        finally:
            first.close()
        recovered = AppInstance(self.root)
        recovered.close()

    def test_finished_measurement_requires_user_review(self):
        session = CalibrationSession(self.root)
        identity = session.start_trial()
        session.finish_trial(identity, measurement(True))
        session.close()
        resumed = CalibrationSession.latest(self.root)
        self.assertEqual(resumed.pending_review()["id"], identity)
        self.assertEqual(resumed.assessment()["quiet_samples"], 0)
        resumed.review(identity, True)
        self.assertEqual(resumed.assessment()["quiet_samples"], 1)

    def test_redo_preserves_the_discarded_attempt(self):
        session = CalibrationSession(self.root)
        first = session.start_trial(); session.finish_trial(first, measurement(True)); session.review(first, False)
        second = session.start_trial()
        self.assertEqual(session._trial(first)["state"], "discarded")
        self.assertEqual(session._trial(first)["step"], session._trial(second)["step"])
        self.assertEqual(len(session.data["trials"]), 2)

    def test_crash_resume_marks_unfinished_capture_interrupted(self):
        session = CalibrationSession(self.root)
        identity = session.start_trial(); session.recording(identity)
        resumed = CalibrationSession.latest(self.root)
        self.assertEqual(resumed._trial(identity)["state"], "interrupted")
        self.assertEqual(resumed.current_step()["key"], "quiet-1")

    def test_complete_dataset_and_optional_skip_are_durable(self):
        session = CalibrationSession(self.root)
        for _ in range(6):
            quiet = session.current_step()["kind"] == "quiet"
            identity = session.start_trial(); session.finish_trial(identity, measurement(quiet)); session.review(identity, True)
        session.skip_wake(); session.close()
        result = json.loads(session.path.read_text(encoding="utf-8"))
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["assessment"]["status"], "ready_for_review")
        self.assertEqual(result["assessment"]["level_contrast_db"], 20)
        self.assertEqual(result["assessment"]["recommended_capture_shift"], 14)
        self.assertEqual(result["assessment"]["wake_check"], "skipped")
        self.assertFalse(result["assessment"]["settings_changed"])
        self.assertTrue((self.root / "latest-complete.json").exists())

    def test_raw_recording_or_extra_payload_cannot_be_persisted(self):
        value = measurement(); value["raw_pcm"] = "not-allowed"
        with self.assertRaises(ValueError): clean_measurement(value)
        value = measurement(); value["windows"][0]["transcript"] = "not-allowed"
        with self.assertRaises(ValueError): clean_measurement(value)

    def test_atomic_replace_failure_preserves_original_json(self):
        path = self.root / "record.json"
        atomic_json(path, {"saved": 1})
        with mock.patch.object(Path, "replace", side_effect=PermissionError()), mock.patch("microphone_calibration_core.time.sleep"):
            with self.assertRaises(PermissionError): atomic_json(path, {"saved": 2})
        self.assertEqual(json.loads(path.read_text()), {"saved": 1})

    def test_changed_gain_cannot_mix_with_previous_trials(self):
        session = CalibrationSession(self.root)
        session.set_policy({"pcm_shift": 14, "samples": 64000})
        session.start_trial()
        with self.assertRaises(ValueError): session.set_policy({"pcm_shift": 15, "samples": 64000})

    def test_demo_cannot_resume_as_real_measurements(self):
        session = CalibrationSession(self.root, demo=True)
        self.assertIsNone(CalibrationSession.latest(self.root, demo=False))
        self.assertTrue(json.loads(session.path.read_text())["demo"])

    def test_usb_writer_rejects_cloud_and_unverified_capture_commands(self):
        device = CalibratorDevice(lambda *args: None, threading.Event(), threading.Event())
        device.connection = FakeSerial()
        with self.assertRaises(CheckFailure): device.send(b"n")
        device.trusted = True
        for command in [b"v", b"g", b"j", b"w", b"~private"]:
            with self.assertRaises(CheckFailure): device.send(command)
        self.assertFalse(device.connection.writes)

    def test_identity_hold_and_release_serial_contract(self):
        class Connection(FakeSerial):
            is_open = False
            def open(self): self.is_open = True
            def close(self): self.is_open = False
        client = RobotClient(lambda *args: None)
        client.device.connection = Connection(
            ("LocalCompanion: CAPTURE_POLICY samples=64000 bytes=128000 duration_ms=4000 sample_rate=16000 worker_timeout_ms=5500 pcm_shift=14\n"
                f"CompanionSession: PROTOCOL version=1 mac={EXPECTED_MAC} configured=1 busy=0\n").encode(),
            b"MicrophonePrep: CALIBRATOR_PROTOCOL version=1 lease_ms=10000 recording_event=1\n"
                b"LocalCompanion: STATUS state=IDLE capture=local-4s pending=0\n"
                b"HardwareDiagnostics: STATUS wakes=0 WiFi=off connected=0\n",
            b"MicrophonePrep: CALIBRATOR_CONTROL hold=1 lease_ms=10000\n",
            b"LocalCompanion: STATUS state=IDLE capture=local-4s pending=0\n",
            b"MicrophonePrep: CALIBRATOR_CONTROL hold=0 wake_armed=1\n")
        self.assertEqual(client.connect()["samples"], 64000)
        self.assertTrue(client.device.held)
        client.close()
        self.assertEqual(client.device.connection.writes, [b"s", b"s", b"[", b"s", b"]"])
        self.assertFalse(client.device.connection.is_open)


if __name__ == "__main__": unittest.main()
