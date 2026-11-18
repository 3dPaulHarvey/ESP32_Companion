"""Autosaved calibration sessions. Contains only numeric levels and user checks."""
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
import time
import uuid

from check_esp32_session import EXPECTED_MAC

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "logs" / "microphone-calibration"
QA_RESULTS = ROOT / "logs" / "microphone-calibration-qa"
STEPS = tuple([{"key": f"quiet-{i}", "kind": "quiet", "number": i, "total": 3} for i in range(1, 4)] +
    [{"key": f"speech-{i}", "kind": "speech", "number": i, "total": 3} for i in range(1, 4)] +
    [{"key": "wake-1", "kind": "wake", "number": 1, "total": 1, "optional": True}])
MEASUREMENT_KEYS = {"request", "samples", "bytes", "duration_ms", "elapsed_ms", "rms", "peak", "clipped",
    "pcm_shift", "raw_peak", "legacy_clipped", "raw_near_full_scale", "suggested_shift", "dc_mean", "ac_rms", "windows"}
WINDOW_KEYS = {"request", "second", "samples", "rms", "peak", "clipped"}


class AppInstance:
    """One UI writer per results folder, including across double-click launches."""
    def __init__(self, root):
        import msvcrt
        root = Path(root)
        root.mkdir(parents=True, exist_ok=True)
        self.file = (root / "app.lock").open("a+b")
        self.file.seek(0, 2)
        if not self.file.tell():
            self.file.write(b"0")
            self.file.flush()
        self.file.seek(0)
        try:
            msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            self.file.close()
            raise RuntimeError("The microphone calibrator is already open. Use its existing window.") from None

    def close(self):
        self.file.close()  # The OS releases this byte-range lock, even after a crash.


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def atomic_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for attempt in range(5):
        try:
            temporary.replace(path)
            break
        except PermissionError:
            if attempt == 4: raise
            time.sleep(.05 * (attempt + 1))


def clean_measurement(value):
    if not isinstance(value, dict) or set(value) != MEASUREMENT_KEYS:
        raise ValueError("Unexpected measurement fields; nothing raw can be saved.")
    result = {}
    for key in MEASUREMENT_KEYS - {"windows"}:
        if type(value[key]) is not int:
            raise ValueError("Measurement values must be integers.")
        result[key] = value[key]
    if not isinstance(value["windows"], list) or len(value["windows"]) != 4:
        raise ValueError("Four window summaries are required.")
    result["windows"] = []
    for window in value["windows"]:
        if not isinstance(window, dict) or set(window) != WINDOW_KEYS or any(type(v) is not int for v in window.values()):
            raise ValueError("Unexpected window data.")
        result["windows"].append(dict(window))
    return result


class CalibrationSession:
    def __init__(self, root=RESULTS, *, demo=False, data=None, folder=None):
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.demo = demo
        if data is None:
            identity = datetime.now().astimezone().strftime("%Y-%m-%d_%H-%M-%S") + "_" + uuid.uuid4().hex[:8]
            self.folder = (self.root / identity).resolve()
            self.data = {"schema_version": 1, "session_id": identity, "created_at": utc_now(),
                "local_created_at": datetime.now().astimezone().isoformat(), "device_mac": EXPECTED_MAC, "port": "COM26",
                "demo": demo, "status": "in_progress", "policy": None, "trials": [],
                "steps": [{**step, "status": "pending"} for step in STEPS],
                "raw_audio_saved": False, "microphone_audio_uploaded": False}
        else:
            self.folder = Path(folder).resolve()
            if (data.get("schema_version") != 1 or data.get("demo") != demo or data.get("device_mac") != EXPECTED_MAC or
                [step.get("key") for step in data.get("steps", [])] != [step["key"] for step in STEPS]):
                raise ValueError("The saved session is not compatible with this calibrator.")
            self.data = data
            self.data["resumed_at"] = utc_now()
            self.data["status"] = "in_progress"
            for trial in self.data["trials"]:
                if trial["state"] in {"countdown", "recording"}:
                    trial.update(state="interrupted", ended_at=utc_now())
        if not self.folder.is_relative_to(self.root):
            raise ValueError("Session path is outside its results folder.")
        self.path = self.folder / "session.json"
        self.save()

    @classmethod
    def latest(cls, root=RESULTS, *, demo=False):
        root = Path(root).resolve()
        try:
            pointer = json.loads((root / "latest-session.json").read_text(encoding="utf-8"))
            folder = (root / pointer["session_id"]).resolve()
            if not folder.is_relative_to(root): return None
            data = json.loads((folder / "session.json").read_text(encoding="utf-8"))
            if data.get("status") == "complete": return None
            return cls(root, demo=demo, data=data, folder=folder)
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def current_step(self):
        return next((step for step in self.data["steps"] if step["status"] == "pending"), None)

    def pending_review(self):
        step = self.current_step()
        if not step: return None
        return next((trial for trial in reversed(self.data["trials"]) if trial["step"] == step["key"] and trial["state"] == "review"), None)

    def set_policy(self, policy):
        policy = {key: int(value) for key, value in policy.items()}
        previous = self.data["policy"]
        if previous is not None and previous != policy and self.data["trials"]:
            raise ValueError("Capture settings changed. Start a new calibration session.")
        self.data["policy"] = policy
        self.save()

    def start_trial(self):
        step = self.current_step()
        if not step or self.pending_review(): raise ValueError("Review the previous sample first.")
        trial = {"id": uuid.uuid4().hex[:12], "step": step["key"], "kind": step["kind"],
            "state": "countdown", "started_at": utc_now(), "accepted": False}
        self.data["trials"].append(trial)
        self.save()
        return trial["id"]

    def _trial(self, identity):
        return next(trial for trial in self.data["trials"] if trial["id"] == identity)

    def recording(self, identity):
        self._trial(identity)["state"] = "recording"
        self.save()

    def finish_trial(self, identity, measurement):
        trial = self._trial(identity)
        trial.update(state="review", ended_at=utc_now(), measurement=clean_measurement(measurement))
        self.save()

    def stop_trial(self, identity, *, error=None):
        trial = self._trial(identity)
        trial.update(state="error" if error else "cancelled", ended_at=utc_now())
        if error: trial["error"] = error
        self.save()

    def review(self, identity, keep):
        trial = self._trial(identity)
        if trial["state"] != "review": raise ValueError("This sample is not awaiting review.")
        trial.update(state="accepted" if keep else "discarded", accepted=bool(keep), reviewed_at=utc_now())
        if keep:
            trial["user_confirmed_conditions"] = True
            trial["phrase_finished_in_window"] = trial["kind"] != "quiet"
            next(step for step in self.data["steps"] if step["key"] == trial["step"]).update(status="accepted", trial_id=identity)
        self.save()

    def skip_wake(self):
        step = self.current_step()
        if not step or not step.get("optional"): raise ValueError("Only the optional wake check can be skipped.")
        step.update(status="skipped", reason="user_skipped")
        self.save()

    def required_complete(self):
        return all(step["status"] == "accepted" for step in self.data["steps"] if not step.get("optional"))

    def assessment(self):
        accepted = [trial for trial in self.data["trials"] if trial.get("accepted")]
        quiet = [trial["measurement"] for trial in accepted if trial["kind"] == "quiet"]
        speech = [trial["measurement"] for trial in accepted if trial["kind"] == "speech"]
        result = {"quiet_samples": len(quiet), "speech_samples": len(speech), "required_complete": self.required_complete(),
            "wake_check": next(step["status"] for step in self.data["steps"] if step["kind"] == "wake"),
            "settings_changed": False, "status": "awaiting_samples"}
        if quiet and speech:
            noise = statistics.median(window["rms"] for capture in quiet for window in capture["windows"])
            voice = statistics.median(max(window["rms"] for window in capture["windows"]) for capture in speech)
            contrast = 20 * math.log10(max(voice, 1) / max(noise, 1))
            clipped = sum(capture["clipped"] for capture in speech)
            raw_full = sum(capture["raw_near_full_scale"] for capture in speech)
            recommended = max(capture["suggested_shift"] for capture in speech)
            result.update(quiet_window_rms=noise, voice_active_window_rms=voice, level_contrast_db=round(contrast, 2),
                clipped_samples=clipped, legacy_clipped_samples=sum(capture["legacy_clipped"] for capture in speech),
                near_full_scale_raw_samples=raw_full, recommended_capture_shift=recommended,
                current_capture_shift=speech[0]["pcm_shift"], maximum_voice_peak=max(capture["peak"] for capture in speech))
            if self.required_complete():
                result["status"] = "ready_for_review" if not clipped and not raw_full and contrast >= 6 else "needs_review"
        return result

    def save(self):
        self.data["updated_at"] = utc_now()
        self.data["assessment"] = self.assessment()
        atomic_json(self.path, self.data)
        pointer = {"session_id": self.data["session_id"], "updated_at": self.data["updated_at"],
            "status": self.data["status"], "required_complete": self.required_complete(), "demo": self.demo}
        atomic_json(self.root / "latest-session.json", pointer)
        if self.required_complete(): atomic_json(self.root / "latest-complete.json", pointer)
        summary = ["ESP32 microphone calibration", "DEMO DATA" if self.demo else "Local robot measurements",
            f"Session: {self.data['session_id']}", f"Status: {self.data['status']}",
            f"Quiet samples kept: {self.data['assessment']['quiet_samples']}/3",
            f"Speech samples kept: {self.data['assessment']['speech_samples']}/3",
            "No microphone audio or transcript was saved or uploaded.", "No settings were changed automatically.",
            "", json.dumps(self.data["assessment"], indent=2)]
        (self.folder / "summary.txt").write_text("\n".join(summary) + "\n", encoding="utf-8")

    def close(self):
        for trial in self.data["trials"]:
            if trial["state"] in {"countdown", "recording"}: trial.update(state="interrupted", ended_at=utc_now())
        self.data["status"] = "complete" if self.required_complete() else "partial"
        self.data["closed_at"] = utc_now()
        self.save()
