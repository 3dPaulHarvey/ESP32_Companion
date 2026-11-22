"""Privacy, partial-save and read-only contracts for the live observer."""

import json
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from check_esp32_session import CheckFailure
from robot_live_observer import LiveLines, LiveReport, ReadOnlyLiveDevice, observe, parse_live_line


class LiveObserverChecks(unittest.TestCase):
    def test_report_retries_temporary_replace_lock(self):
        replace = os.replace
        attempts = []
        def transient(source, target):
            attempts.append(1)
            if len(attempts) == 1:
                raise PermissionError("temporary report lock")
            return replace(source, target)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trial.json"
            with patch("robot_live_observer.os.replace", side_effect=transient), patch("robot_live_observer.time.sleep"):
                LiveReport(path).save()
            self.assertTrue(json.loads(path.read_text())["observer_only"])
            self.assertEqual(len(attempts), 2)
    def test_fixture_metadata_is_explicit_and_bounded(self):
        event = parse_live_line("LiveConversation: LIVE_FIXTURE_BEGIN reused=1 microphone=0")
        self.assertEqual(event["fields"], {"reused": 1, "microphone": 0})
        self.assertEqual(parse_live_line("LiveConversation: CAPTURE request=1 reason=fixture_speech")["fields"]["reason"], "fixture_speech")
        with self.assertRaises(CheckFailure):
            parse_live_line("LiveConversation: LIVE_FIXTURE_BEGIN reused=1 microphone=2")
    def test_partial_lines_and_optional_device_timing(self):
        lines = LiveLines()
        self.assertEqual(lines.feed("I (123) LiveConversation: UI state=ASK_"), [])
        self.assertEqual(lines.feed("NOW turn=1 warm=0\r\n"), [
            {"event": "UI", "device_ms": 123, "fields": {"state": "ASK_NOW", "turn": 1, "warm": 0}}])
        self.assertEqual(lines.feed("OtherTag: transcript is ignored\n"), [])

    def test_multiple_wakes_quiet_and_eight_turns_are_metadata(self):
        lines = LiveLines()
        trace = "LiveConversation: WARM job=7 warm=1 idle_ms=30000 turns=1\n"
        trace += "LiveConversation: WAKE job=7 turn=2 warm=1 reused=1 source=warm_wake\n"
        trace += "LiveConversation: CAPTURE turn=2 samples=64000 frames=200 accepted=0 reason=quiet_timeout audio_uploaded=0\n"
        trace += "LiveConversation: INPUT turn=8 bytes=96000 chunks=75 stream_ended=1 live=1\n"
        events = lines.feed(trace)
        self.assertEqual(len(events), 4)
        self.assertEqual(events[2]["fields"]["audio_uploaded"], 0)
        self.assertEqual(events[3]["fields"]["turn"], 8)

    def test_finalized_firmware_fields_and_failure_codes_are_retained(self):
        trace = ("LiveConversation: LIVE_FEATURE version=1 connection_during_capture=1 warm_wake=1 automatic_replay=0\n"
            "LiveConversation: LIVE_STATUS active=1 pending_capture=9 stage=2 turn=1\n"
            "LiveConversation: UI state=ASK_NOW turn=1 request=9 warm=0 mono_us=123456\n"
            "LiveConversation: TRIGGER request=9 wake_count=3 reused=0 mono_us=123456\n"
            "LiveConversation: CAPTURE request=9 captured_samples=32000 published_samples=24000 frames=100 speech_frames=60 longest=50 accepted=1 endpoint=2 rms=100 peak=500 clipped=0 elapsed_ms=2200 reason=speech_end\n"
            "LiveConversation: INPUT turn=1 request=9 bytes=48000 chunks=75 ended=1 upload_ms=2200 live=1\n"
            "LiveConversation: TIMING turn=1 upload_ms=2200 first_audio_ms=2300 generation_complete_ms=2500 turn_complete_ms=4500 playback_start_ms=2350 end_to_playback_ms=150\n"
            "LiveConversation: WARM remaining_ms=30000 turns=1 connection_open=1\n"
            "LiveConversation: WRITE_FAILURE status=timeout written=17 calls=4 waits=2 partials=1 last_return=-26752\n"
            "LiveConversation: CLEANUP reason=none\n")
        events = LiveLines().feed(trace)
        self.assertEqual(len(events), 10)
        self.assertEqual(events[4]["fields"]["published_samples"], 24000)
        self.assertEqual(events[6]["fields"]["end_to_playback_ms"], 150)
        self.assertEqual(events[8]["fields"]["last_return"], -26752)

    def test_private_or_unrecognized_live_fields_fail_without_echo(self):
        bad_lines = [
            "LiveConversation: TURN turn=1 transcript=private_phrase",
            "LiveConversation: RESULT ok=0 reason=private_phrase",
            "LiveConversation: WARM warm=1 url=wss://private.example/auth_tokens/x",
            "LiveConversation: SECRET value=123",
            "LiveConversation: INPUT bytes=10 bytes=20",
            "LiveConversation: INPUT bytes=-1",
            "LiveConversation: TURN turn=9",
            "LiveConversation: CAPTURE accepted=2",
            "LiveConversation: TIMING mono_us=99999999999999999999",
            "LiveConversation: CAPTURE reason=\"quiet\"",
        ]
        for line in bad_lines:
            with self.subTest(line=line), self.assertRaises(CheckFailure) as error:
                parse_live_line(line)
            self.assertNotIn("private_phrase", str(error.exception))
            self.assertNotIn("private.example", str(error.exception))

    def test_partial_or_complete_oversized_lines_are_bounded(self):
        for trace in ["x" * 4097, "x" * 4097 + "\n"]:
            with self.assertRaises(CheckFailure):
                LiveLines().feed(trace)

    def test_ansi_logs_parse_but_arbitrary_prefixes_do_not(self):
        result = parse_live_line("\x1b[0;32mI (50) LiveConversation: RESULT ok=1 reason=idle_timeout turns=2\x1b[0m")
        self.assertEqual(result["fields"]["reason"], "idle_timeout")
        with self.assertRaises(CheckFailure):
            parse_live_line("private prefix LiveConversation: TURN turn=1")

    def test_atomic_report_preserves_partial_events_and_no_raw_text(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.json"
            report = LiveReport(path)
            report.add(LiveLines().feed("LiveConversation: UI state=CONNECTING turn=1\n"))
            report.save()
            first = json.loads(path.read_text(encoding="utf-8"))
            self.assertFalse(first["finished"])
            self.assertEqual(first["events"][0]["fields"]["state"], "CONNECTING")
            report.add(LiveLines().feed("LiveConversation: CAPTURE turn=1 reason=quiet_timeout audio_uploaded=0\n"))
            report.data["finished"] = True
            report.save()
            final = json.loads(path.read_text(encoding="utf-8"))
            self.assertTrue(final["finished"])
            self.assertEqual(len(final["events"]), 2)
            self.assertFalse(final["raw_serial_saved"])
            self.assertFalse(path.with_name(path.name + ".tmp").exists())

    def test_device_rejects_every_control_except_status_without_opening_serial(self):
        device = ReadOnlyLiveDevice()
        for command in (b"c", b"x", b"g", b"w", b"o", b"p", b"=", b"n", b"ss", b"s\n"):
            with self.assertRaises(CheckFailure):
                device.send(command)
        with self.assertRaises(CheckFailure):
            device.reset()
        self.assertFalse(device.connection.is_open)

    def test_observation_saves_prior_metadata_when_a_later_field_is_rejected(self):
        class Connection:
            def __init__(self):
                self.is_open = False
                self.writes = []
                self.chunks = []
            @property
            def in_waiting(self):
                return len(self.chunks[0]) if self.chunks else 0
            def open(self): self.is_open = True
            def close(self): self.is_open = False
            def flush(self): pass
            def write(self, value):
                self.writes.append(value)
                self.chunks.extend([
                    ("LocalCompanion: CAPTURE_POLICY samples=64000 bytes=128000 duration_ms=4000 sample_rate=16000 worker_timeout_ms=5500 pcm_shift=15\n"
                     "CompanionSession: PROTOCOL version=1 mac=44:bd:8d:ec:38:e0 configured=1 busy=1\n"
                     "LiveConversation: UI state=ASK_NOW turn=1 request=2 warm=0 mono_us=1000\n").encode(),
                    b"LiveConversation: TURN turn=1 transcript=private_phrase\n",
                ])
            def read(self, _):
                return self.chunks.pop(0) if self.chunks else b""
        device = ReadOnlyLiveDevice()
        device.connection = Connection()
        with tempfile.TemporaryDirectory() as directory, patch("sys.stdout", new=io.StringIO()):
            path = Path(directory) / "session.json"
            self.assertFalse(observe(1, LiveReport(path), device=device))
            text = path.read_text(encoding="utf-8")
            data = json.loads(text)
            self.assertTrue(data["finished"])
            self.assertIsNotNone(data["failure"])
            self.assertEqual(data["events"][0]["fields"]["state"], "ASK_NOW")
            self.assertNotIn("private_phrase", text)
            self.assertEqual(device.connection.writes, [b"s"])
            self.assertFalse(device.connection.is_open)


if __name__ == "__main__":
    unittest.main()
