"""Self-paced wake, follow-up and quiet-timeout test, with local autosave."""
import argparse
from datetime import datetime
import os
from pathlib import Path
import queue
import time
import tkinter as tk
from tkinter import ttk, messagebox
import uuid

from microphone_calibration_core import AppInstance, atomic_json, utc_now, ROOT
from microphone_calibrator import Worker
from microphone_calibration_robot import DemoRobot, TrialCancelled
from robot_conversation_client import ConversationClient


class DemoConversation(DemoRobot):
    def connect(self):
        self.callback("preflight", {"measurement": {"passed": True, "questions_sent": 0, "audio_messages_sent": 0, "demo": True}})
        return {"samples": 64000, "bytes": 128000, "duration_ms": 4000, "pcm_shift": 15, "conversation_max_turns": 2}

    def pause(self):
        for _ in range(8):
            if self.cancel.is_set() or self.closing.is_set(): raise TrialCancelled()
            time.sleep(.05)

    def capture(self, kind):
        self.callback("waiting_wake", {}); self.pause()
        self.callback("wake_triggered", {"cycle": 1, "wake_count": 1})
        turns = 2 if kind == "followup" else 1
        captures = []; outputs = []
        for after in range(turns + 1):
            if after: self.callback("followup_window", {"after_turn": after})
            speak = after < turns
            self.callback("recording", {"duration_ms": 4000, "after_turn": after, "expect_speech": speak})
            self.pause(); self.callback("capture_end", {"after_turn": after})
            capture = {"after_turn": after, "measurement": {"request": after + 1, "samples": 64000, "bytes": 128000,
                "duration_ms": 4000, "pcm_shift": 15, "rms": 900 if speak else 100, "peak": 6000 if speak else 500, "clipped": 0}}
            captures.append(capture); self.callback("microphone", capture)
            if after:
                self.callback("vad", {"after_turn": after, "frames": 200, "speech_frames": 50 if speak else 0, "longest": 20 if speak else 0, "accepted": int(speak)})
            if speak:
                self.callback("connecting", {"turn": after + 1}); self.pause()
                self.callback("speaking", {"turn": after + 1}); self.pause()
                out = {"turn": after + 1, "measurement": {"played_samples": 48000, "duration_ms": 2000, "clipped": 0}}
                outputs.append(out); self.callback("turn_result", out)
        self.callback("ready", {})
        return {"completed_turns": turns, "same_connection": True, "normal_close": True, "quiet_timeout": True,
            "captures": captures, "outputs": outputs, "demo": True}


class ConversationApp:
    def __init__(self, window, *, demo=False, results=None):
        self.window = window; self.demo = demo
        self.root = Path(results or ROOT / "logs" / ("robot-conversation-tests-qa" if demo else "robot-conversation-tests"))
        identity = datetime.now().astimezone().strftime("%Y-%m-%d_%H-%M-%S") + "_" + uuid.uuid4().hex[:8]
        self.folder = self.root / identity
        self.data = {"schema_version": 1, "demo": demo, "session_id": identity, "created_at": utc_now(), "status": "in_progress",
            "policy": None, "attempts": [], "raw_audio_saved": False, "transcript_saved": False, "conversation_max_turns": 2,
            "audio_upload_requires_armed_wake": True, "followup_ms": 4000}
        self.events = queue.Queue(); self.factory = DemoConversation if demo else ConversationClient
        self.worker = Worker(self.events, client_factory=self.factory)
        self.connected = self.busy = self.closing = self.save_failed = False
        self.current = None; self.recording_started = None; self.expect_speech = True; self.phase = "disconnected"
        window.title("Robot Follow-up Conversation Test" + (" - Demo" if demo else ""))
        window.geometry("1000x760"); window.minsize(1000, 760); window.configure(bg="#edf2f7")
        window.protocol("WM_DELETE_WINDOW", self.finish)
        ttk.Style().configure("TButton", font=("Segoe UI", 11), padding=10)
        panel = tk.Frame(window, bg="#edf2f7", padx=26, pady=22); panel.pack(fill="both", expand=True)
        tk.Label(panel, text="Ask a follow-up without another wake word", bg="#edf2f7", fg="#23344a", font=("Segoe UI", 22, "bold")).pack(anchor="w")
        tk.Label(panel, text="Run the quiet check first, then the two-question check twice. Each trial starts when you click Arm.\nSpeak to the robot from 1-2 feet away. No recording or transcript is saved.",
            bg="#edf2f7", fg="#53677d", font=("Segoe UI", 11), justify="left").pack(anchor="w", pady=(8, 15))
        options = tk.Frame(panel, bg="#edf2f7"); options.pack(fill="x", pady=(0, 12))
        self.exercise = tk.StringVar(value="quiet")
        self.radios = [ttk.Radiobutton(options, text=label, value=value, variable=self.exercise) for label, value in
            [("1. First answer, then stay quiet", "quiet"), ("2. Two questions, then stay quiet", "followup")]]
        for radio in self.radios: radio.pack(side="left", padx=(0, 20))
        card = tk.Frame(panel, bg="white", padx=24, pady=18); card.pack(fill="both", expand=True)
        self.phrase = tk.StringVar(value='First: "What is two plus two?"')
        tk.Label(card, textvariable=self.phrase, bg="white", fg="#176b89", font=("Segoe UI", 22, "bold"), wraplength=880, justify="center").pack(fill="x", pady=(0, 16))
        self.status = tk.StringVar(value="Connect when you are ready")
        self.status_label = tk.Label(card, textvariable=self.status, bg="white", fg="#176b89", font=("Segoe UI", 28, "bold"))
        self.status_label.pack(fill="x", pady=(6, 8))
        self.hint = tk.StringVar(value="Connect COM26 and close other robot apps or serial monitors.")
        tk.Label(card, textvariable=self.hint, bg="white", fg="#53677d", font=("Segoe UI", 11), wraplength=870, justify="center").pack(fill="x", pady=(0, 18))
        row = tk.Frame(card, bg="white"); row.pack(fill="x")
        self.connect_button = ttk.Button(row, text="Connect robot", command=self.connect); self.connect_button.pack(side="left")
        self.arm_button = ttk.Button(row, text="Arm conversation", command=self.start); self.arm_button.pack(side="left", padx=10)
        self.stop_button = ttk.Button(row, text="Stop", command=self.stop); self.stop_button.pack(side="left")
        self.confirm = tk.Frame(card, bg="white")
        self.yes_button = ttk.Button(self.confirm, text="Yes - correct speech and quiet idle", command=lambda: self.feedback(True)); self.yes_button.pack(side="left")
        ttk.Button(self.confirm, text="Answer, audio or timeout problem", command=lambda: self.feedback(False)).pack(side="left", padx=10)
        self.saved_text = tk.StringVar()
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
            self.save_failed = True; self.worker.cancel.set(); self.retry_button.pack(side="right", padx=10)
            self.saved_text.set("Save failed. Previous files are preserved. Keep this window open and use Retry save."); return False
        self.save_failed = False; self.retry_button.pack_forget()
        self.saved_text.set("DEMO results saved separately." if self.demo else "Autosaved. Finish & close when done; Codex can review the data later.")
        return True

    def retry_save(self): self.save(); self.refresh()

    def refresh(self):
        pending = self.current and self.current["state"] == "awaiting_confirmation"
        self.connect_button.configure(state="disabled" if self.connected or self.busy or self.closing else "normal")
        enabled = self.connected and not self.busy and not pending and not self.closing and not self.save_failed
        self.arm_button.configure(state="normal" if enabled else "disabled")
        self.stop_button.configure(state="normal" if self.busy and self.phase != "connecting" and not self.closing else "disabled")
        quiet_passed = any(a["exercise"] == "quiet" and a["state"] == "verified_by_user" and a["human_confirmed"] is True for a in self.data["attempts"])
        for index, radio in enumerate(self.radios):
            radio.configure(state="disabled" if self.busy or pending or self.closing or (index == 1 and not quiet_passed) else "normal")
        if pending and not self.closing: self.confirm.pack(fill="x", pady=(18, 0))
        else: self.confirm.pack_forget()

    def connect(self):
        if not self.worker.is_alive(): self.worker = Worker(self.events, client_factory=self.factory); self.worker.start()
        self.busy = True; self.phase = "connecting"; self.status.set("Checking the connection...")
        self.hint.set("Wait while the robot, Wi-Fi and Gemini setup are checked. No question starts yet.")
        self.refresh(); self.worker.commands.put(("connect", {}))

    def start(self):
        if not self.connected or self.busy or self.save_failed: return
        if self.exercise.get() == "followup" and not any(a["exercise"] == "quiet" and a["state"] == "verified_by_user" and a["human_confirmed"] is True for a in self.data["attempts"]): return
        self.current = {"started_at": utc_now(), "state": "waiting_wake", "exercise": self.exercise.get(), "human_confirmed": None,
            "captures": [], "turn_results": [], "vad": []}
        self.data["attempts"].append(self.current)
        if not self.save(): self.refresh(); return
        self.worker.cancel.clear()  # A new click clears an earlier Stop/save failure, before queuing work.
        self.busy = True; self.phase = "conversation"; self.phrase.set('First: "What is two plus two?"')
        self.status_label.configure(fg="#176b89"); self.status.set("Arming...")
        self.refresh(); self.worker.commands.put(("capture", {"kind": self.current["exercise"]}))

    def stop(self):
        self.worker.cancel.set(); self.status.set("Stopping safely...")
        self.hint.set("Waiting for capture, connection and speaker cleanup.")

    def feedback(self, passed):
        self.current.update(state="verified_by_user" if passed else "user_reported_problem", human_confirmed=passed, reviewed_at=utc_now())
        self.save(); self.status.set("Result saved")
        if passed and self.current["exercise"] == "quiet": self.exercise.set("followup")
        self.hint.set("Run the two-question check twice with a fresh wake each time, then Finish & close."); self.refresh()

    def finish(self):
        if self.closing: return
        if not self.worker.is_alive():
            self.data.update(status="closed", closed_at=utc_now())
            if self.save(): self.window.destroy()
            return
        self.closing = True; self.worker.cancel.set(); self.worker.closing.set()
        self.recording_started = None; self.status.set("Saving and closing...")
        self.hint.set("Closing Gemini, restoring local wake and turning Wi-Fi off."); self.refresh()

    def process_events(self):
        try:
            while True:
                name, data, when = self.events.get_nowait()
                if name == "closed":
                    if self.current and self.current["state"] not in {"verified_by_user", "user_reported_problem", "awaiting_confirmation", "cancelled", "error"}:
                        self.current.update(state="interrupted", ended_at=utc_now())
                    self.data.update(status="closed", closed_at=utc_now())
                    if self.save(): self.window.destroy(); return
                    self.closing = self.connected = self.busy = False; self.refresh(); continue
                if name == "connected":
                    self.connected = True; self.busy = False; self.phase = "idle"; self.data["policy"] = data; self.save()
                    self.status.set("Ready when you are"); self.hint.set('Click Arm conversation, then say "Hi, E-S-P" once.')
                elif name == "preflight": self.data["cloud_preflight"] = data["measurement"]; self.save()
                elif name == "waiting_wake":
                    self.status.set("Say Hi, E-S-P"); self.hint.set("Wait for green before asking the first question. Stop cancels this arm.")
                elif name == "wake_triggered" and self.current:
                    self.current["wake_trigger"] = data; self.save(); self.status.set("Wake heard - preparing...")
                elif name == "recording" and self.current:
                    self.current["state"] = "recording"; self.recording_started = when; self.expect_speech = data["expect_speech"]; self.save()
                    if self.expect_speech:
                        self.status_label.configure(fg="#138362")
                        self.phrase.set('"What is two plus two?"' if data["after_turn"] == 0 else '"And what is that number times three?"')
                        self.hint.set("Speak to the robot now. Ask once at normal volume; stop at the cue.")
                    else:
                        self.status_label.configure(fg="#176b89"); self.phrase.set("Stay quiet and let the robot return to READY")
                        self.hint.set("Keep background audio paused. This window checks that silence closes the conversation.")
                elif name in {"capture_end", "connecting", "speaking", "ready"}:
                    self.recording_started = None; self.status_label.configure(fg="#176b89")
                    self.status.set("Listen to answer " + str(data.get("turn", "")) if name == "speaking" else "Checking / waiting...")
                    self.hint.set("After the answer, follow the next green or STAY QUIET instruction.")
                    if name == "speaking" and self.current:
                        self.phrase.set('Next: "And what is that number times three?"' if data.get("turn") == 1 and self.current["exercise"] == "followup"
                            else "Next: stay quiet and let the robot return to READY")
                elif name == "microphone" and self.current: self.current["captures"].append(data); self.save()
                elif name == "turn_result" and self.current: self.current["turn_results"].append(data); self.save()
                elif name == "vad" and self.current: self.current["vad"].append(data); self.save()
                elif name == "result" and self.current:
                    self.recording_started = None; self.busy = False; self.phase = "idle"
                    self.current.update(state="awaiting_confirmation", ended_at=utc_now(), measurement=data); self.save()
                    full = self.current["exercise"] == "followup"
                    self.status.set("Four, then twelve?" if full else "Four, then quiet idle?")
                    self.yes_button.configure(text="Yes - four, then twelve, then idle" if full else "Yes - four, then quiet idle")
                    self.hint.set("Confirm only if the answers were correct, speech was clear, and the robot returned to READY after silence.")
                elif name == "cancelled":
                    self.recording_started = None; self.busy = False; self.phase = "idle"
                    if self.current: self.current.update(state="cancelled", ended_at=utc_now()); self.save()
                    self.status.set("Cancelled - ready to retry")
                elif name == "error":
                    self.recording_started = None; self.busy = self.connected = False; self.phase = "disconnected"
                    if self.current: self.current.update(state="error", ended_at=utc_now(), error=data["message"])
                    self.data["last_error"] = data["message"]; self.save(); self.status.set("Reconnect and retry"); self.hint.set(data["message"])
                self.refresh()
        except queue.Empty: pass
        if self.recording_started is not None and not self.closing:
            left = max(0, 4 - (time.monotonic() - self.recording_started))
            self.status.set(("SPEAK NOW" if self.expect_speech else "STAY QUIET") + f"  {left:.1f}s")
        self.window.after(50, self.process_events)


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--demo", action="store_true"); args = parser.parse_args()
    window = tk.Tk(); window.withdraw(); instance = None
    try:
        results = ROOT / "logs" / ("robot-conversation-tests-qa" if args.demo else "robot-conversation-tests")
        instance = AppInstance(results); ConversationApp(window, demo=args.demo, results=results)
        window.deiconify(); window.mainloop()
    except (OSError, RuntimeError, ValueError) as error:
        messagebox.showerror("Robot conversation test", str(error), parent=window); window.destroy()
    finally:
        if instance: instance.close()


if __name__ == "__main__": main()
