"""Question upload/completion gates, USB allowlist and isolated Tk workflow."""
import json
from pathlib import Path
import tempfile
import threading
import time
import tkinter as tk
import unittest
import sys
from unittest import mock

from robot_question_client import QuestionDevice, question_metadata, CheckFailure
from robot_question_test import QuestionApp, DemoQuestion
from test_esp32_voice_tool import VALID
from test_esp32_session_tool import FakeSerial

TRACE = ("CompanionSession: REQUEST_BEGIN kind=question no_audio_upload=0\nCompanionGemini: CONNECT_BEGIN no_question=0 no_audio_upload=0\n" +
    VALID.replace("CompanionVoice:", "CompanionQuestion:").replace("no_audio_upload=1", "no_audio_upload=0") +
    "\nCompanionQuestion: INPUT bytes=128000 chunks=100 stream_ended=1 sample_rate=16000 input_cleared=1\n")


class QuestionChecks(unittest.TestCase):
    def test_exact_input_and_complete_reply(self):
        result = question_metadata(TRACE)
        self.assertTrue(result["input_cleared"])
        self.assertEqual(result["output"]["played_samples"], 48000)

    def test_partial_upload_and_missing_eof_fail(self):
        for old, new in [("bytes=128000", "bytes=64000"), ("chunks=100", "chunks=99"),
                         ("stream_ended=1", "stream_ended=0"), ("input_cleared=1", "input_cleared=0")]:
            with self.subTest(field=old), self.assertRaises(CheckFailure): question_metadata(TRACE.replace(old, new))

    def test_reply_failures_remain_failures(self):
        for old, new in [("turn_complete=1", "turn_complete=0"), ("played_samples=48000", "played_samples=47999"),
                         ("ok=1", "ok=0"), ("close_ack=1", "close_ack=0")]:
            with self.subTest(field=old), self.assertRaises(CheckFailure): question_metadata(TRACE.replace(old, new))

    def test_no_duplicate_request_or_connect(self):
        for extra in ["CompanionSession: REQUEST_BEGIN kind=question", "CompanionGemini: CONNECT_BEGIN"]:
            with self.assertRaises(CheckFailure): question_metadata(TRACE + extra)

    def test_only_verified_board_can_start_upload(self):
        d = QuestionDevice(lambda *args: None, threading.Event(), threading.Event())
        d.connection = FakeSerial()
        for data in [b"q", b"j"]:
            with self.assertRaises(CheckFailure): d.send(data)
        d.trusted = True
        for data in [b"~credential", b"g", b"v"]:
            with self.assertRaises(CheckFailure): d.send(data)
        self.assertFalse(d.connection.writes)

    def test_split_speaking_event_is_observed_once(self):
        events = []
        d = QuestionDevice(lambda name, data: events.append(name), threading.Event(), threading.Event())
        d.connection = FakeSerial()
        d.connection.reads = []
        d.observing_capture = True; d.job_start = 0; d.seen_states = set()
        d.connection = FakeSerial(b"CompanionQuestion: UI state=SPEA", b"KING wake_armed=0\n")
        d.read(); d.read()
        self.assertEqual(events.count("speaking"), 1)

    def test_cloud_preflight_failure_keeps_start_disabled(self):
        class Unavailable(DemoQuestion):
            def connect(self): raise CheckFailure("Cloud preflight failed; no microphone recording started.")
        base = Path(__file__).resolve().parent.parent / "logs" / "question-ui-tests"
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temp:
            window = tk.Tk(); app = QuestionApp(window, demo=True, results=Path(temp))
            app.worker.client_factory = Unavailable
            try:
                app.connect()
                deadline = time.monotonic() + 4
                while time.monotonic() < deadline and app.busy:
                    window.update(); time.sleep(.01)
                self.assertFalse(app.connected)
                self.assertEqual(str(app.start_button['state']), "disabled")
                self.assertEqual(app.data['attempts'], [])
                self.assertIn('Cloud preflight failed', app.data['last_error'])
            finally:
                app.finish()
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline and app.worker.is_alive():
                    window.update(); time.sleep(.01)
                window.update()
                try: window.destroy()
                except tk.TclError: pass

    def test_click_flow_autosaves_and_closes_during_capture(self):
        base = Path(__file__).resolve().parent.parent / "logs" / "question-ui-tests"
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temp:
            window = tk.Tk(); app = QuestionApp(window, demo=True, results=Path(temp))
            callback_errors = []
            window.report_callback_exception = lambda *args: callback_errors.append(args[1])
            def until(condition, timeout=8):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    window.update()
                    if condition(): break
                    time.sleep(.01)
                else: self.fail("Question UI did not reach the expected state.")
                self.assertFalse(callback_errors, callback_errors)
            try:
                app.connect(); until(lambda: app.connected)
                self.assertEqual(app.data["attempts"], [])
                self.assertTrue(app.data["cloud_preflight"]["passed"])
                self.assertEqual(app.data["cloud_preflight"]["audio_messages_sent"], 0)
                app.start(); app.stop()
                self.assertEqual(app.current["state"], "cancelled")
                app.start(); until(lambda: app.current["state"] == "awaiting_confirmation")
                window.update_idletasks()
                self.assertTrue(app.confirm.winfo_ismapped())
                self.assertLessEqual(app.finish_button.winfo_rooty() + app.finish_button.winfo_height(), window.winfo_rooty() + window.winfo_height())
                saved = json.loads((app.folder / "session.json").read_text(encoding="utf-8"))
                self.assertIsNone(saved["attempts"][-1]["human_confirmed"])
                app.feedback(True)
                self.assertTrue(app.current["human_confirmed"])
                with mock.patch("robot_question_test.atomic_json", side_effect=OSError()):
                    app.save()
                self.assertTrue(app.save_failed)
                app.retry_save(); self.assertFalse(app.save_failed)
                app.start(); until(lambda: app.recording_started is not None)
                app.finish()
                until(lambda: json.loads((app.folder / "session.json").read_text(encoding="utf-8"))["status"] == "closed")
                saved = json.loads((app.folder / "session.json").read_text(encoding="utf-8"))
                self.assertTrue(saved["demo"])
                self.assertEqual(saved["attempts"][1]["state"], "verified_by_user")
                self.assertIn(saved["attempts"][2]["state"], {"cancelled", "interrupted"})
            finally:
                app.worker.closing.set(); app.worker.cancel.set(); app.worker.join(timeout=3)
                try: window.destroy()
                except tk.TclError: pass


if __name__ == "__main__": unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
