"""Self-paced first spoken question test. Start controls every cloud upload."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import queue
import time
import tkinter as tk
from tkinter import ttk, messagebox
import uuid

from microphone_calibration_core import AppInstance, atomic_json, utc_now, ROOT
from microphone_calibrator import Worker
from microphone_calibration_robot import DemoRobot
from robot_question_client import QuestionClient


class DemoQuestion(DemoRobot):
    def connect(self):
        return {**super().connect(), "pcm_shift": 15}

    def capture(self, kind):
        microphone = super().capture("speech")
        microphone["pcm_shift"] = 15
        self.callback("microphone", {"measurement": microphone})
        self.callback("connecting", {})
        self.callback("speaking", {})
        self.callback("ready", {})
        return {"input_bytes": 128000, "input_chunks": 100, "input_stream_ended": True,
            "input_cleared": True, "output": {"ok": 1, "audio_bytes": 96000, "played_samples": 48000}, "microphone": microphone}


class QuestionApp:
    def __init__(self, window, *, demo=False, results=None):
        self.window = window
        self.demo = demo
        self.root = Path(results or ROOT / "logs" / ("robot-question-tests-qa" if demo else "robot-question-tests"))
        identity = datetime.now().astimezone().strftime("%Y-%m-%d_%H-%M-%S") + "_" + uuid.uuid4().hex[:8]
        self.folder = self.root / identity
        self.data = {"schema_version": 1, "demo": demo, "session_id": identity, "created_at": utc_now(),
            "status": "in_progress", "policy": None, "attempts": [], "raw_audio_saved": False,
            "transcript_saved": False, "audio_upload_requires_start": True}
        self.events = queue.Queue()
        self.worker = Worker(self.events, client_factory=DemoQuestion if demo else QuestionClient)
        self.connected = False; self.busy = False; self.closing = False
        self.current = None; self.countdown_job = None; self.recording_started = None
        self.phase = "disconnected"
        self.save_failed = False
        window.title("Robot Spoken Question Test" + (" — Demo" if demo else ""))
        window.geometry("920x650"); window.minsize(920, 650)
        window.configure(bg="#edf2f7")
        window.protocol("WM_DELETE_WINDOW", self.finish)
        ttk.Style().configure("TButton", font=("Segoe UI", 11), padding=10)
        panel = tk.Frame(window, bg="#edf2f7", padx=28, pady=24); panel.pack(fill="both", expand=True)
        tk.Label(panel, text="Ask your robot one question", bg="#edf2f7", fg="#23344a", font=("Segoe UI", 22, "bold")).pack(anchor="w")
        tk.Label(panel, text="Each Start sends one four-second microphone capture to Gemini.\nNo recording or transcript is saved. Connecting alone starts no question.",
            bg="#edf2f7", fg="#53677d", font=("Segoe UI", 11), justify="left").pack(anchor="w", pady=(8, 18))
        card = tk.Frame(panel, bg="white", padx=24, pady=20); card.pack(fill="both", expand=True)
        tk.Label(card, text="Sit 1–2 feet from the robot. Wait for green, then say:", bg="white", fg="#23344a", font=("Segoe UI", 12)).pack(anchor="w")
        tk.Label(card, text="“What is two plus two?”", bg="white", fg="#176b89", font=("Segoe UI", 24, "bold")).pack(anchor="w", pady=12)
        self.status = tk.StringVar(value="Connect when you are ready")
        self.status_label = tk.Label(card, textvariable=self.status, bg="white", fg="#176b89", font=("Segoe UI", 28, "bold"))
        self.status_label.pack(fill="x", pady=(8, 5))
        self.hint = tk.StringVar(value="Connect the COM26 robot and close any serial monitor.")
        tk.Label(card, textvariable=self.hint, bg="white", fg="#53677d", font=("Segoe UI", 11), wraplength=800, justify="center").pack(fill="x", pady=(0, 16))
        row = tk.Frame(card, bg="white"); row.pack(fill="x")
        self.connect_button = ttk.Button(row, text="Connect robot", command=self.connect); self.connect_button.pack(side="left")
        self.start_button = ttk.Button(row, text="Start question", command=self.start); self.start_button.pack(side="left", padx=10)
        self.stop_button = ttk.Button(row, text="Stop", command=self.stop); self.stop_button.pack(side="left")
        self.confirm = tk.Frame(card, bg="white")
        ttk.Button(self.confirm, text="Yes — clear answer: four", command=lambda: self.feedback(True)).pack(side="left")
        ttk.Button(self.confirm, text="Answer or audio problem", command=lambda: self.feedback(False)).pack(side="left", padx=10)
        self.saved_text = tk.StringVar(value="Autosave is on. You can stop and close at any time.")
        tk.Label(panel, textvariable=self.saved_text, bg="#edf2f7", fg="#53677d", font=("Segoe UI", 10)).pack(anchor="w", pady=12)
        bottom = tk.Frame(panel, bg="#edf2f7"); bottom.pack(fill="x")
        ttk.Button(bottom, text="Open saved folder", command=lambda: os.startfile(str(self.folder))).pack(side="left")
        self.finish_button = ttk.Button(bottom, text="Finish & close", command=self.finish); self.finish_button.pack(side="right")
        self.retry_button = ttk.Button(bottom, text="Retry save", command=self.retry_save)
        self.save(); self.refresh(); self.worker.start(); window.after(50, self.process_events)

    def save(self):
        self.data["updated_at"] = utc_now()
        try:
            atomic_json(self.folder / "session.json", self.data)
            atomic_json(self.root / "latest-session.json", {"session_id": self.data["session_id"], "demo": self.demo, "status": self.data["status"]})
        except OSError:
            self.save_failed = True
            self.retry_button.pack(side="right", padx=10)
            self.saved_text.set("Save failed. Previous files are preserved. Keep this window open and use Retry save.")
            self.worker.cancel.set()
            return False
        self.save_failed = False; self.retry_button.pack_forget()
        self.saved_text.set("DEMO results saved separately." if self.demo else "Autosaved. Finish & close when done; Codex can read the results later.")
        return True

    def retry_save(self):
        self.save(); self.refresh()

    def refresh(self):
        self.connect_button.configure(state="disabled" if self.connected or self.busy or self.closing else "normal")
        pending = self.current and self.current["state"] == "awaiting_confirmation"
        self.start_button.configure(state="normal" if self.connected and not self.busy and not pending and not self.closing and not self.save_failed else "disabled")
        self.stop_button.configure(state="normal" if self.busy and self.phase != "connecting" and not self.closing else "disabled")
        if pending and not self.closing: self.confirm.pack(fill="x", pady=(14, 0))
        else: self.confirm.pack_forget()

    def connect(self):
        if not self.worker.is_alive():
            self.worker = Worker(self.events, client_factory=DemoQuestion if self.demo else QuestionClient)
            self.worker.start()
        self.busy = True; self.phase = "connecting"; self.status.set("Connecting…")
        self.hint.set("Verifying the robot and connecting its saved Wi-Fi. Wait before speaking.")
        self.refresh(); self.worker.commands.put(("connect", {}))

    def start(self):
        if not self.connected or self.busy: return
        self.current = {"started_at": utc_now(), "state": "countdown", "human_confirmed": None}
        self.data["attempts"].append(self.current)
        if not self.save(): self.refresh(); return
        self.busy = True; self.phase = "countdown"; self.refresh()
        self.countdown(1 if self.demo else 3)

    def countdown(self, left):
        if self.closing or self.phase != "countdown": return
        if left:
            self.status.set(str(left)); self.hint.set("Get ready. Wait for SPEAK NOW.")
            self.countdown_job = self.window.after(1000, lambda: self.countdown(left - 1))
        else:
            self.countdown_job = None; self.phase = "question"; self.status.set("Preparing…")
            self.worker.commands.put(("capture", {"kind": "question"}))

    def stop(self):
        if self.phase == "countdown":
            if self.countdown_job: self.window.after_cancel(self.countdown_job)
            self.countdown_job = None
            self.current.update(state="cancelled", ended_at=utc_now()); self.save()
            self.busy = False; self.phase = "idle"; self.status.set("Cancelled — ready to retry"); self.refresh()
        else:
            self.worker.cancel.set(); self.status.set("Stopping safely…")
            self.hint.set("Waiting for capture/network cleanup. A reply already playing may finish first.")

    def feedback(self, passed):
        self.current.update(state="verified_by_user" if passed else "user_reported_problem", human_confirmed=passed, reviewed_at=utc_now())
        self.save(); self.status.set("Result saved"); self.hint.set("Finish & close, or start another question when ready."); self.refresh()

    def finish(self):
        if self.closing: return
        if not self.worker.is_alive():
            self.data.update(status="closed", closed_at=utc_now())
            if self.save(): self.window.destroy()
            return
        self.closing = True
        if self.countdown_job: self.window.after_cancel(self.countdown_job)
        self.worker.cancel.set(); self.worker.closing.set()
        self.status.set("Saving and closing…"); self.hint.set("Restoring wake detection and turning the robot's Wi-Fi off."); self.refresh()

    def process_events(self):
        try:
            while True:
                name, data, when = self.events.get_nowait()
                if name == "closed":
                    if self.current and self.current["state"] in {"countdown", "recording"}:
                        self.current.update(state="interrupted", ended_at=utc_now())
                    self.data.update(status="closed", closed_at=utc_now())
                    if self.save(): self.window.destroy(); return
                    self.closing = False; self.connected = False; self.busy = False
                    self.refresh(); continue
                if name == "connected":
                    self.connected = True; self.busy = False; self.phase = "idle"
                    self.data["policy"] = data; self.save()
                    self.status.set("Ready when you are"); self.hint.set("Click Start question. No need to coordinate with chat.")
                elif name == "recording" and self.current:
                    self.current["state"] = "recording"; self.recording_started = when; self.save()
                    self.status_label.configure(fg="#138362"); self.hint.set("Speak to the ROBOT now. Say the question once at normal volume.")
                elif name == "microphone" and self.current:
                    self.current["microphone"] = data["measurement"]; self.save()
                elif name in {"capture_end", "connecting", "speaking", "ready"}:
                    self.recording_started = None; self.status_label.configure(fg="#176b89")
                    self.status.set("Listen to the robot" if name == "speaking" else "Waiting for Gemini…")
                    self.hint.set("Stop speaking after the cue. Wait for its spoken answer and READY.")
                elif name == "result" and self.current:
                    self.recording_started = None; self.busy = False; self.phase = "idle"
                    self.current.update(state="awaiting_confirmation", ended_at=utc_now(), measurement=data); self.save()
                    self.status.set("Did it say four clearly?"); self.hint.set("Confirm only if the answer was correct, speech sounded normal, and the robot returned to READY.")
                elif name == "cancelled":
                    self.recording_started = None; self.busy = False; self.phase = "idle"
                    if self.current: self.current.update(state="cancelled", ended_at=utc_now()); self.save()
                    self.status.set("Cancelled — ready to retry")
                elif name == "error":
                    self.recording_started = None; self.busy = False; self.connected = False; self.phase = "disconnected"
                    if self.current and self.current["state"] in {"countdown", "recording"}:
                        self.current.update(state="error", ended_at=utc_now(), error=data["message"])
                    self.save(); self.status.set("Reconnect and retry"); self.hint.set(data["message"])
                self.refresh()
        except queue.Empty: pass
        if self.recording_started is not None and not self.closing:
            self.status.set(f"SPEAK NOW  {max(0, 4 - (time.monotonic() - self.recording_started)):.1f}s")
        self.window.after(50, self.process_events)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", action="store_true")
    args = parser.parse_args()
    window = tk.Tk(); window.withdraw(); instance = None
    try:
        root = ROOT / "logs" / ("robot-question-tests-qa" if args.demo else "robot-question-tests")
        instance = AppInstance(root)
        QuestionApp(window, demo=args.demo, results=root)
        window.deiconify(); window.mainloop()
    except (OSError, RuntimeError, ValueError) as error:
        messagebox.showerror("Robot question test", str(error), parent=window)
        window.destroy()
    finally:
        if instance: instance.close()


if __name__ == "__main__": main()
