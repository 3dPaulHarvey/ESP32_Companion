"""Exercise live streaming with public PCM on an explicitly flashed fixture image."""
import argparse
import json
from pathlib import Path
import time
import re

from check_esp32_microphone import MicrophoneDevice
from check_esp32_session import CheckFailure
from check_standalone_device import wait_network
from robot_live_observer import LiveLines, LiveReport


class FixtureDevice(MicrophoneDevice):
    def __init__(self, report):
        super().__init__()
        self.report = report
        self.trusted = False

    def send(self, data):
        if data not in {b"s", b"F"} or (data == b"F" and not self.trusted):
            raise CheckFailure("The live fixture checker permits only verified fixture/status commands.")
        if data == b"s":
            self.report.data["status_commands"] += 1
        return super().send(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--warm-reuse", action="store_true")
    args = parser.parse_args()
    if args.report.exists():
        parser.error("Choose a new evidence file.")
    report = LiveReport(args.report)
    report.data.update(observer_only=False, synthetic_public_pcm=True, microphone=False,
                       fixture_commands=0, warm_reuse=args.warm_reuse)
    report.save()
    device = FixtureDevice(report)
    started_at = time.monotonic()
    failure = None
    try:
        device.connection.open()
        configured, busy = device.protocol(require_capture=True)
        if not configured or busy:
            raise CheckFailure("An idle configured development robot is required.")
        device.trusted = True
        start = device.send(b"s")
        device.wait(r"Standalone: FEATURE version=1 boot_wifi=1 auto_wake=0", start)
        device.wait(r"CompanionQuestion: FIXTURE_FEATURE enabled=1 microphone=0", start)
        wait_network(device)
        start = device.send(b"F")
        report.data["fixture_commands"] = 1
        framing = LiveLines()
        previous = start
        reused = False
        terminal = None
        deadline = time.monotonic() + 155
        while time.monotonic() < deadline:
            device.read()
            events = framing.feed(device.text[previous:])
            previous = len(device.text)
            report.add(events)
            for event in events:
                if event["event"] in {"INPUT", "TURN", "WARM", "RESULT", "CAPTURE"}:
                    print(json.dumps(event), flush=True)
                if event["event"] == "WARM" and args.warm_reuse and not reused:
                    device.send(b"F")
                    reused = True
                    report.data["fixture_commands"] += 1
                if event["event"] == "RESULT":
                    terminal = event["fields"]
            report.save()
            if terminal:
                break
        if not terminal:
            raise CheckFailure("Live fixture did not reach a terminal result.")
        expected = 4 if args.warm_reuse else 2
        assert terminal["ok"] == 1 and terminal["fixture"] == 1, terminal
        assert terminal["http"] == 200 and terminal["upgrade_http"] == 101 and terminal["setup_ack"] == 1
        assert terminal["turns"] == expected and terminal["close_ack"] == 1 and terminal["close_code"] == 1000
        assert terminal["reason"] == "idle_timeout" and terminal["quiet_discards"] == (2 if args.warm_reuse else 1)
        inputs = [e["fields"] for e in report.data["events"] if e["event"] == "INPUT"]
        outputs = [e["fields"] for e in report.data["events"] if e["event"] == "TURN"]
        captures = [e["fields"] for e in report.data["events"] if e["event"] == "CAPTURE"]
        assert len(inputs) == len(outputs) == expected
        assert all(i["bytes"] == 128000 and i["chunks"] == 100 and i["ended"] == 1 for i in inputs)
        assert all(o["audio_bytes"] == o["played_samples"] * 2 and 0 < o["played_samples"] <= 240000 and o["clipped"] == 0 for o in outputs)
        assert all(c["published_samples"] == 0 for c in captures if not c["accepted"])
        faults = [e["fields"]["injected"] for e in report.data["events"] if e["event"] == "WRITE_FAULT"]
        assert faults == ["header_wait", "header_short", "payload_short", "payload_want_write"]
        writes = [e["fields"] for e in report.data["events"] if e["event"] == "WRITE_STATS"]
        assert len(writes) == 1 and writes[0]["waits"] >= 2 and writes[0]["partials"] >= 2 and writes[0]["question_retries"] == 0
        text = device.text[start:]
        assert text.count("CompanionSession: REQUEST_BEGIN kind=live_conversation_fixture") == 1
        assert text.count("CompanionGemini: CONNECT_BEGIN") == 1
        assert "QuestionStream: RECORDING" not in text and "MicrophonePrep: RECORDING" not in text
        ready = device.send(b"s")
        device.wait(r"CompanionVoice: STATUS active=0 speaking=0 wake_armed=1 playback_busy=0", ready)
        report.data["passed"] = True
    except Exception as error:
        failure = type(error).__name__
        if isinstance(error, OSError):
            report.data["os_failure"] = {"errno": error.errno, "winerror": getattr(error, "winerror", None),
                                         "file": Path(error.filename).name if error.filename else None}
        if isinstance(error, CheckFailure):
            report.data["controlled_failure"] = str(error)
            print("Controlled check:", str(error), flush=True)
        report.data["passed"] = False
        print("Live fixture stopped:", failure, flush=True)
    finally:
        device.connection.close()
        heaps = re.findall(r"HEAP internal_free=(\d+) internal_min=(\d+) internal_largest=(\d+) psram_total=(\d+) psram_free=(\d+)", device.text)
        report.data["heap_samples"] = [dict(zip(["internal_free", "internal_min", "internal_largest", "psram_total", "psram_free"], map(int, row))) for row in heaps]
        report.data.update(finished=True, failure=failure, elapsed_ms=int((time.monotonic() - started_at) * 1000))
        report.save()
    return 1 if failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
