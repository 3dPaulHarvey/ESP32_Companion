"""Prepare four seconds of verified synthetic PCM for developer-only hardware QA."""
import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent.parent
    source = root / "logs" / "gemini-input-question.pcm"
    metadata = source.with_suffix(".json")
    if not source.exists() or not metadata.exists():
        from make_audio_fixture import main as synthesize
        synthesize()  # Fixed local System.Speech output to file; no microphone.
    data = source.read_bytes()
    info = json.loads(metadata.read_text(encoding="utf-8"))
    if (info.get("source") != "Windows System.Speech synthetic fixture" or info.get("question") != "What is two plus two?" or
        info.get("microphone_opened") is not False or (info.get("sample_rate"), info.get("channels"), info.get("sample_bits")) != (16000, 1, 16) or
        not 0 < len(data) <= 128000 or len(data) % 2 or info.get("bytes") != len(data) or
        hashlib.sha256(data).hexdigest() != info.get("sha256")):
        raise RuntimeError("The fixed synthetic fixture failed identity/format verification.")
    target = root / "logs" / "robot-question-public-fixture.pcm"
    target.write_bytes(data + bytes(128000 - len(data)))
    print("Verified public synthetic question prepared: 128000 bytes, four seconds, no microphone data.")


if __name__ == "__main__": main()
