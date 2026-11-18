"""Read the latest user-run calibration after the GUI has been closed."""
import argparse
import json
from pathlib import Path
from microphone_calibration_core import RESULTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--latest-partial", action="store_true", help="Read the latest run even if incomplete")
    args = parser.parse_args()
    pointer_path = RESULTS / ("latest-session.json" if args.latest_partial else "latest-complete.json")
    if not pointer_path.exists():
        print("No " + ("user session" if args.latest_partial else "complete user calibration") + " is saved yet.")
        return
    try:
        pointer = json.loads(pointer_path.read_text(encoding="utf-8"))
        folder = (RESULTS / pointer["session_id"]).resolve()
        if not folder.is_relative_to(RESULTS.resolve()): raise ValueError()
        path = folder / "session.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("demo") is not False or data.get("schema_version") != 1: raise ValueError()
        if not args.latest_partial and data.get("status") != "complete":
            print("The required samples are saved, but this session is still open. Use Finish & close before reviewing it.")
            return
        print(json.dumps({"file": str(path), "status": data["status"], "closed_at": data.get("closed_at"),
            "policy": data["policy"], "assessment": data["assessment"], "attempts_saved": len(data["trials"]),
            "raw_audio_saved": data["raw_audio_saved"], "microphone_audio_uploaded": data["microphone_audio_uploaded"]}, indent=2))
    except (OSError, ValueError, KeyError, TypeError):
        parser.exit(1, "The saved-session pointer is invalid; no private file contents were printed.\n")


if __name__ == "__main__": main()
