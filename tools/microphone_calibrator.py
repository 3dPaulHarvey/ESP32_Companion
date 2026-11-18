"""Click-to-run Windows microphone calibrator; every trial is autosaved locally."""
import argparse
import os
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

import serial

from microphone_calibration_core import AppInstance, CalibrationSession, RESULTS, QA_RESULTS
from microphone_calibration_robot import RobotClient, DemoRobot, TrialCancelled, CheckFailure


class Worker(threading.Thread):
    def __init__(self, events, *, demo=False):
        super().__init__(daemon=True)
        self.events = events
        self.commands = queue.Queue()
        self.cancel = threading.Event()
        self.closing = threading.Event()
        self.client = None
        self.demo = demo

    def emit(self, name, data=None):
        self.events.put((name, data or {}, time.monotonic()))

    def run(self):
        try:
            while not self.closing.is_set():
                try:
                    command, data = self.commands.get(timeout=.05)
                except queue.Empty:
                    command, data = "idle", {}
                try:
                    if command == "connect":
                        self.cancel.clear()
                        if self.client:
                            try: self.client.close()
                            except Exception: pass
                        self.client = (DemoRobot if self.demo else RobotClient)(self.emit, self.cancel, self.closing)
                        self.emit("connected", self.client.connect())
                    elif command == "capture" and self.client:
                        self.emit("result", self.client.capture(data["kind"]))
                    elif self.client:
                        self.client.poll()
                except TrialCancelled:
                    self.cancel.clear()
                    self.emit("cancelled")
                except (CheckFailure, serial.SerialException, OSError, ValueError) as error:
                    message = str(error) if isinstance(error, CheckFailure) else (
                        "Cannot access COM26. Close serial monitors, check the development robot's USB cable, and reconnect.")
                    if self.client:
                        try: self.client.close()
                        except Exception: pass
                    self.client = None
                    self.emit("error", {"message": message})
                except Exception:
                    if self.client:
                        try: self.client.close()
                        except Exception: pass
                    self.client = None
                    self.emit("error", {"message": "The calibration operation stopped unexpectedly. Previous samples are saved; reconnect and retry."})
        finally:
            if self.client:
                try: self.client.close()
                except Exception: pass  # The firmware lease restores wake if USB is lost.
            self.emit("closed")


class CalibratorApp:
    def __init__(self, window, *, demo=False):
        self.window = window
        self.demo = demo
        self.results = QA_RESULTS if demo else RESULTS
        self.session = CalibrationSession.latest(self.results, demo=demo) or CalibrationSession(self.results, demo=demo)
        self.events = queue.Queue()
        self.worker = Worker(self.events, demo=demo)
        self.connected = False
        self.busy = False
        self.closing = False
        self.trial = None
        self.countdown_job = None
        self.recording_started = None
        self.phase = "disconnected"
        self.save_problem = False
        self.window.title("ESP32 Microphone Calibrator" + (" — Demo" if demo else ""))
        self.window.geometry("960x790")
        self.window.minsize(960, 790)
        self.window.configure(bg="#edf2f7")
        self.window.protocol("WM_DELETE_WINDOW", self.finish)
        self.style = ttk.Style()
        self.style.theme_use("clam")
        self.style.configure("TButton", font=("Segoe UI", 11), padding=(14, 9))
        self.style.configure("Primary.TButton", background="#176b89", foreground="white", font=("Segoe UI", 12, "bold"))
        self.style.map("Primary.TButton", background=[("active", "#125a74"), ("disabled", "#b6c6ce")])
        self.style.configure("TProgressbar", background="#16866a", troughcolor="#e7eef3", thickness=12)
        self._build()
        self.refresh()
        self.worker.start()
        self.window.after(50, self.process_events)

    def _label(self, parent, text="", **options):
        defaults = {"bg": "white", "fg": "#23344a", "font": ("Segoe UI", 11), "anchor": "w", "justify": "left"}
        defaults.update(options)
        label = tk.Label(parent, text=text, **defaults)
        return label

    def _build(self):
        outer = tk.Frame(self.window, bg="#edf2f7", padx=28, pady=22)
        outer.pack(fill="both", expand=True)
        title = self._label(outer, "Robot microphone check", bg="#edf2f7", font=("Segoe UI", 23, "bold"))
        title.pack(anchor="w")
        self._label(outer, "Run each sample when you are ready. Speak to the robot, not your PC.", bg="#edf2f7", fg="#53677d").pack(anchor="w", pady=(5, 14))
        self.connection_text = tk.StringVar(value="Demo preview — no robot connection" if self.demo else "Development robot · COM26 · not connected")
        row = tk.Frame(outer, bg="#edf2f7")
        row.pack(fill="x", pady=(0, 14))
        self._label(row, textvariable=self.connection_text, bg="#edf2f7", fg="#53677d").pack(side="left")
        self.connect_button = ttk.Button(row, text="Connect robot" if not self.demo else "Connect demo", command=self.connect)
        self.connect_button.pack(side="right")
        self.card = tk.Frame(outer, bg="white", padx=24, pady=20, highlightthickness=1, highlightbackground="#d9e3ec")
        self.card.pack(fill="both", expand=True)
        self.step_title = tk.StringVar()
        self._label(self.card, textvariable=self.step_title, font=("Segoe UI", 17, "bold")).pack(anchor="w")
        self.instructions = tk.StringVar()
        self._label(self.card, textvariable=self.instructions, wraplength=820).pack(anchor="w", pady=(10, 8))
        self.phrase = tk.StringVar()
        self._label(self.card, textvariable=self.phrase, font=("Segoe UI", 21, "bold"), fg="#125a74", wraplength=810).pack(anchor="w", pady=(2, 12))
        self.capture_frame = tk.Frame(self.card, bg="white")
        self.capture_frame.pack(fill="x")
        self.big_text = tk.StringVar(value="Ready when you are")
        self.big_label = self._label(self.capture_frame, textvariable=self.big_text, font=("Segoe UI", 35, "bold"), anchor="center", fg="#176b89")
        self.big_label.pack(fill="x", pady=(4, 2))
        self.hint = tk.StringVar()
        self._label(self.capture_frame, textvariable=self.hint, anchor="center", fg="#53677d", wraplength=810).pack(fill="x", pady=(3, 9))
        self.progress = ttk.Progressbar(self.capture_frame, mode="determinate", maximum=100)
        self.progress.pack(fill="x", pady=(0, 14))
        actions = tk.Frame(self.capture_frame, bg="white")
        actions.pack(fill="x")
        self.start_button = ttk.Button(actions, text="Start this sample", style="Primary.TButton", command=self.start)
        self.start_button.pack(side="left")
        self.stop_button = ttk.Button(actions, text="Stop sample", command=self.stop)
        self.stop_button.pack(side="left", padx=10)
        self.skip_button = ttk.Button(actions, text="Skip optional wake check", command=self.skip)
        self.skip_button.pack(side="right")
        self.review_frame = tk.Frame(self.card, bg="#f2f7fa", padx=14, pady=10)
        self.review_text = tk.StringVar()
        self._label(self.review_frame, textvariable=self.review_text, bg="#f2f7fa", wraplength=780).pack(anchor="w")
        review_actions = tk.Frame(self.review_frame, bg="#f2f7fa")
        review_actions.pack(fill="x", pady=(8, 0))
        self.keep_button = ttk.Button(review_actions, text="Keep this sample", command=lambda: self.review(True))
        self.keep_button.pack(side="left")
        ttk.Button(review_actions, text="Redo this sample", command=lambda: self.review(False)).pack(side="left", padx=10)
        self.saved_text = tk.StringVar(value="Autosave is on. No recordings or transcripts are saved or uploaded.")
        self._label(outer, textvariable=self.saved_text, bg="#edf2f7", fg="#53677d", wraplength=900).pack(anchor="w", pady=(12, 8))
        bottom = tk.Frame(outer, bg="#edf2f7")
        bottom.pack(fill="x")
        self.new_button = ttk.Button(bottom, text="New session", command=self.new_session)
        self.new_button.pack(side="left")
        ttk.Button(bottom, text="Open saved folder", command=self.open_folder).pack(side="left", padx=10)
        self.finish_button = ttk.Button(bottom, text="Finish & close", command=self.finish)
        self.finish_button.pack(side="right")
        self.retry_save_button = ttk.Button(bottom, text="Retry save", command=self.retry_save)

    def saved(self, action):
        try:
            result = action()
            self.save_problem = False
            self.retry_save_button.pack_forget()
            self.saved_text.set("DEMO: test data saved separately." if self.demo else "Autosaved. You can finish and close at any time; partial sessions are kept too.")
            return result
        except (OSError, ValueError) as error:
            self.save_problem = True
            self.retry_save_button.pack(side="right", padx=8)
            self.saved_text.set("Saving failed. The previous saved file has been preserved.")
            messagebox.showerror("Could not save", str(error) if isinstance(error, ValueError) else "Check that the project folder is writable, then try again.", parent=self.window)
            return False

    def retry_save(self):
        if self.saved(self.session.save) is not False: self.refresh()

    def refresh(self):
        step = self.session.current_step()
        review = self.session.pending_review()
        if self.closing:
            self.big_text.set("Saving and closing…")
            self.hint.set("Releasing COM26 and restoring local wake detection.")
        elif self.save_problem:
            self.big_text.set("Check autosave")
            self.hint.set("The last saved file is intact. Resolve the folder issue, then click Retry save.")
        elif not self.connected and not self.busy:
            self.big_text.set("Connect your robot" if not self.demo else "Demo preview")
            self.hint.set("Close any serial monitor, then click Connect robot." if not self.demo else "No USB is used. Preview results cannot count as calibration.")
        elif self.connected and not self.busy:
            self.big_text.set("Sample saved" if review else "Ready when you are")
            self.big_label.configure(fg="#176b89")
            self.progress["value"] = 100 if review else 0
            self.hint.set("Keep only if you followed the instructions. Clipping is useful data—do not hide it by retrying." if review else "Click Start. Wait for the green signal; no need to coordinate with Codex.")
        if not step:
            self.step_title.set("All steps saved — ready for review")
            self.instructions.set("You can close the program now. Codex can read the saved measurements later. No gain settings have been changed automatically.")
            self.phrase.set("")
            if self.connected and not self.closing: self.big_text.set("Finished")
        elif step["kind"] == "quiet":
            self.step_title.set(f"Quiet baseline · sample {step['number']} of 3")
            self.instructions.set("Pause background audio and stay silent. The robot will capture four seconds after the countdown.")
            self.phrase.set("Stay quiet during the green window.")
        elif step["kind"] == "speech":
            self.step_title.set(f"Normal speech · sample {step['number']} of 3")
            self.instructions.set("Sit 1–2 feet from the robot's microphone. When SPEAK NOW appears, say the sentence once at your normal volume. Stop at the robot's end cue.")
            self.phrase.set("“What is two plus two?”")
        else:
            self.step_title.set("Optional: wake-word check")
            self.instructions.set("Click Start, then say “Hi, E-S-P” when prompted. Once SPEAK NOW appears, say the question. You may skip this extra check.")
            self.phrase.set("“Hi, E-S-P” → “What is two plus two?”")
        if review:
            self.capture_frame.pack_forget()
            self.trial = review["id"]
            value = review["measurement"]
            condition = "you stayed silent" if review["kind"] == "quiet" else "you spoke normally and finished the sentence before the cue"
            self.review_text.set(f"Recorded {value['duration_ms'] / 1000:.1f} seconds · Peak {value['peak'] / 32768:.0%} · Clipped samples: {value['clipped']}\nKeep it if {condition}. Otherwise choose Redo. Every attempt remains saved.")
            self.review_frame.pack(fill="x", pady=(14, 0))
        else:
            self.review_frame.pack_forget()
            self.capture_frame.pack(fill="x")
        self.connect_button.configure(state="disabled" if self.connected or self.busy or self.closing else "normal")
        self.start_button.configure(state="normal" if self.connected and not self.busy and not review and step and not self.closing and not self.save_problem else "disabled")
        self.stop_button.configure(state="normal" if self.busy and self.phase in {"countdown", "capture"} and not self.closing else "disabled")
        self.new_button.configure(state="disabled" if self.busy or self.closing else "normal")
        self.keep_button.configure(state="normal" if self.connected and not self.busy else "disabled")
        self.skip_button.configure(state="normal" if self.connected and not self.busy and step and step.get("optional") and not review else "disabled")

    def connect(self):
        if not self.worker.is_alive():
            self.worker = Worker(self.events, demo=self.demo)
            self.worker.start()
        self.busy = True; self.phase = "connecting"
        self.big_text.set("Connecting…")
        self.hint.set("Checking COM26 and preparing local capture. No cloud connection is opened.")
        self.refresh()
        self.worker.commands.put(("connect", {}))

    def start(self):
        if self.busy or not self.connected: return
        self.trial = self.saved(self.session.start_trial)
        if not self.trial: return
        self.busy = True; self.phase = "countdown"
        self.review_frame.pack_forget()
        self.progress["value"] = 0
        self.refresh()
        self.countdown(1 if self.demo else 3)

    def countdown(self, remaining):
        if self.phase != "countdown" or self.closing: return
        if remaining:
            self.big_text.set(str(remaining)); self.big_label.configure(fg="#176b89")
            self.hint.set("Get ready. Wait for the green window before speaking.")
            self.countdown_job = self.window.after(1000, lambda: self.countdown(remaining - 1))
        else:
            self.countdown_job = None; self.phase = "capture"
            self.big_text.set("Preparing…")
            self.hint.set("Wait for the robot's recording signal.")
            self.worker.commands.put(("capture", {"kind": self.session.current_step()["kind"]}))

    def stop(self):
        if self.phase == "countdown":
            if self.countdown_job: self.window.after_cancel(self.countdown_job)
            self.countdown_job = None
            self.saved(lambda: self.session.stop_trial(self.trial))
            self.trial = None; self.busy = False; self.phase = "idle"
            self.refresh()
        elif self.phase == "capture":
            self.worker.cancel.set()
            self.big_text.set("Stopping…")
            self.hint.set("Waiting for safe capture cleanup. This attempt is saved as cancelled.")

    def review(self, keep):
        if self.saved(lambda: self.session.review(self.trial, keep)) is False: return
        self.trial = None
        self.refresh()

    def skip(self):
        if self.saved(self.session.skip_wake) is False: return
        self.refresh()

    def new_session(self):
        if self.busy: return
        if self.saved(self.session.close) is False: return
        policy = self.session.data["policy"]
        self.session = CalibrationSession(self.results, demo=self.demo)
        if self.connected and policy: self.saved(lambda: self.session.set_policy(policy))
        self.trial = None
        self.refresh()

    def open_folder(self):
        os.startfile(str(self.session.folder))

    def finish(self):
        if self.closing: return
        if not self.worker.is_alive():
            if self.saved(self.session.close) is not False: self.window.destroy()
            return
        self.closing = True
        if self.countdown_job: self.window.after_cancel(self.countdown_job)
        self.worker.cancel.set(); self.worker.closing.set()
        self.big_text.set("Saving and closing…")
        self.hint.set("Releasing COM26 and restoring local wake detection.")
        self.refresh()

    def process_events(self):
        try:
            while True:
                name, data, event_time = self.events.get_nowait()
                if name == "closed":
                    if self.saved(self.session.close) is not False:
                        self.window.destroy()
                        return
                    self.closing = False; self.connected = False; self.busy = False
                    self.refresh()
                    continue
                if self.closing:
                    if name == "result" and self.trial: self.saved(lambda: self.session.finish_trial(self.trial, data))
                    elif name == "cancelled" and self.trial: self.saved(lambda: self.session.stop_trial(self.trial))
                    continue
                if name == "connected":
                    self.connected = True; self.busy = False; self.phase = "idle"
                    self.saved(lambda: self.session.set_policy(data))
                    self.connection_text.set("Demo connected — simulated values" if self.demo else "Robot connected · COM26 · local capture only")
                    self.refresh()
                elif name == "waiting_wake":
                    self.big_text.set("Say “Hi, E-S-P”")
                    self.hint.set("Say the wake phrase to the robot. The question comes after SPEAK NOW.")
                elif name in {"preparing", "wake_detected"}:
                    self.big_text.set("Get ready…")
                    self.hint.set("The robot is clearing its input buffer. Wait for green.")
                elif name == "recording":
                    self.saved(lambda: self.session.recording(self.trial))
                    self.recording_started = event_time
                    self.big_label.configure(fg="#138362")
                    self.hint.set("Stay silent now." if self.session.current_step()["kind"] == "quiet" else "Speak to the ROBOT now. Say the question once, at normal volume.")
                elif name == "capture_end":
                    self.recording_started = None
                    self.big_text.set("Capture finished")
                    self.big_label.configure(fg="#176b89")
                    self.hint.set("Stop speaking. Checking the saved level measurements…")
                    self.progress["value"] = 100
                elif name == "result":
                    self.recording_started = None
                    self.saved(lambda: self.session.finish_trial(self.trial, data))
                    self.busy = False; self.phase = "idle"
                    self.refresh()
                elif name == "cancelled":
                    self.recording_started = None
                    if self.trial: self.saved(lambda: self.session.stop_trial(self.trial))
                    self.trial = None; self.busy = False; self.phase = "idle"
                    self.refresh()
                elif name == "error":
                    self.recording_started = None
                    if self.trial: self.saved(lambda: self.session.stop_trial(self.trial, error=data["message"]))
                    self.trial = None; self.busy = False; self.connected = False; self.phase = "disconnected"
                    self.refresh()
                    self.hint.set(data["message"])
                    self.big_text.set("Reconnect and retry")
        except queue.Empty:
            pass
        if self.recording_started is not None and not self.closing:
            elapsed = max(0, time.monotonic() - self.recording_started)
            left = max(0, 4 - elapsed)
            quiet = self.session.current_step()["kind"] == "quiet"
            self.big_text.set(("STAY QUIET" if quiet else "SPEAK NOW") + f"  {left:.1f}s")
            self.progress["value"] = min(100, elapsed * 25)
        self.window.after(50, self.process_events)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", action="store_true", help="Preview UI without USB; data is saved only to the QA folder")
    args = parser.parse_args()
    window = tk.Tk()
    window.withdraw()
    instance = None
    try:
        instance = AppInstance(QA_RESULTS if args.demo else RESULTS)
        CalibratorApp(window, demo=args.demo)
        window.deiconify()
        window.mainloop()
    except (RuntimeError, OSError, ValueError) as error:
        messagebox.showerror("Microphone calibrator", str(error) if isinstance(error, (RuntimeError, ValueError)) else
            "The calibrator could not open its saved-results folder. Ask Codex to check the launcher and folder.", parent=window)
        window.destroy()
    finally:
        if instance: instance.close()


if __name__ == "__main__": main()
