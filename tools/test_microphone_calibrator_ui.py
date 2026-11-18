"""Exercise our Tk event loop and saved UI workflow using isolated demo data."""
import json
from pathlib import Path
import tempfile
import time
import tkinter as tk
import unittest
import sys

import microphone_calibrator as gui


class CalibratorUI(unittest.TestCase):
    def test_self_paced_review_cancel_resume_and_close(self):
        base = Path(__file__).resolve().parent.parent / "logs" / "calibrator-ui-tests"
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temporary:
            original = gui.QA_RESULTS
            gui.QA_RESULTS = Path(temporary)
            window = tk.Tk()
            app = gui.CalibratorApp(window, demo=True)
            errors = []
            window.report_callback_exception = lambda *args: errors.append(args[1])

            def until(condition, timeout=8):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    window.update()
                    if condition(): break
                    time.sleep(.01)
                else: self.fail("The Tk workflow did not reach its expected state.")
                self.assertFalse(errors, errors)

            def fits(widget):
                window.update_idletasks()
                self.assertTrue(widget.winfo_ismapped(), str(widget))
                self.assertGreaterEqual(widget.winfo_rooty(), window.winfo_rooty())
                self.assertLessEqual(widget.winfo_rooty() + widget.winfo_height(), window.winfo_rooty() + window.winfo_height())

            try:
                until(lambda: bool(window.winfo_ismapped()))
                fits(app.finish_button)
                app.connect()
                until(lambda: app.connected)
                self.assertFalse(app.session.data["trials"], "Connecting must not start a recording.")
                app.start(); app.stop()
                self.assertEqual(app.session.data["trials"][-1]["state"], "cancelled")
                app.start()
                until(lambda: app.recording_started is not None)
                self.assertIn("STAY QUIET", app.big_text.get())
                until(lambda: app.session.pending_review() is not None)
                fits(app.keep_button); fits(app.finish_button)
                app.review(False)
                self.assertEqual(app.session.current_step()["key"], "quiet-1")
                for _ in range(6):
                    kind = app.session.current_step()["kind"]
                    app.start()
                    until(lambda: app.recording_started is not None)
                    self.assertIn("STAY QUIET" if kind == "quiet" else "SPEAK NOW", app.big_text.get())
                    until(lambda: app.session.pending_review() is not None)
                    fits(app.keep_button); fits(app.finish_button)
                    app.review(True)
                self.assertTrue(app.session.required_complete())
                app.skip()
                self.assertEqual(app.big_text.get(), "Finished")
                saved = json.loads(app.session.path.read_text(encoding="utf-8"))
                self.assertEqual(saved["assessment"]["quiet_samples"], 3)
                self.assertEqual(saved["assessment"]["speech_samples"], 3)
                self.assertTrue(saved["demo"])
                app.new_session()
                path = app.session.path
                app.start()
                until(lambda: app.recording_started is not None)
                app.finish()
                until(lambda: json.loads(path.read_text(encoding="utf-8"))["status"] == "partial")
                saved = json.loads(path.read_text(encoding="utf-8"))
                self.assertEqual(saved["status"], "partial")
                self.assertIn(saved["trials"][-1]["state"], {"cancelled", "interrupted"})
                print("PASS Tk workflow: explicit starts, countdown/recording, review/redo, six kept samples, optional skip, layout bounds and close during capture")
            finally:
                app.worker.cancel.set(); app.worker.closing.set()
                app.worker.join(timeout=3)
                try: window.destroy()
                except tk.TclError: pass
                gui.QA_RESULTS = original


if __name__ == "__main__": unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
