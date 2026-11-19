"""Single connection, follow-up ownership, silence gating and saved Tk workflow."""
import json
from pathlib import Path
import tempfile
import time
import tkinter as tk
import unittest
from unittest import mock

from robot_conversation_client import ConversationClient, ConversationDevice, conversation_metadata, CheckFailure, TrialCancelled
from robot_conversation_test import ConversationApp
from test_robot_wake import PHYSICAL, CAPTURE_TRACE, ARMED_TRACE, WakeSerial


def capture_trace(request, *, source):
    return CAPTURE_TRACE.replace("request=3", f"request={request}").replace("cycle=3", f"cycle={request}").replace("source=voice_question", f"source={source}")


def network_trace(turns=2, *, fixture=False):
    result = f"CompanionSession: REQUEST_BEGIN kind={'conversation_fixture' if fixture else 'conversation'} no_audio_upload=0\nCompanionGemini: CONNECT_BEGIN conversation=1\n"
    for turn in range(1, turns + 1):
        result += (f"CompanionConversation: INPUT turn={turn} bytes=128000 chunks=100 stream_ended=1 input_cleared=1\n"
            f"CompanionConversation: UI state=SPEAKING turn={turn} wake_armed=0\n"
            f"CompanionConversation: TURN turn={turn} complete=1 audio_bytes=96000 chunks=20 sample_rate=24000 played_samples=48000 duration_ms=2000 rms=1000 peak=5000 clipped=0 playback_ms=2170\n"
            f"CompanionConversation: UI state=FOLLOWUP after_turn={turn} wake_armed=0\n")
        if not fixture: result += capture_trace(turn + 3, source="followup_question")
        result += (f"CompanionConversation: VAD after_turn={turn} frames=200 speech_frames={30 if turn < turns else 0} longest={15 if turn < turns else 0} accepted={int(turn < turns)} mode=3\n")
    result += (f"CompanionConversation: RESULT ok=1 http=200 reason=quiet_timeout upgrade_http=101 setup_ack=1 close_sent=1 close_ack=1 close_code=1000 turns={turns} followup_captures={turns} quiet_discards=1 elapsed_ms=40000 cleanup=1 same_connection=1 fixture={int(fixture)}\n"
        "CompanionConversation: UI state=READY wake_armed=1\n")
    return result


class ConversationSerial(WakeSerial):
    def __init__(self, turns=2): super().__init__(); self.turns = turns
    def write(self, data):
        super().write(data)
        if data == b"c":
            text = ARMED_TRACE + PHYSICAL + capture_trace(3, source="voice_question") + network_trace(self.turns)
            self.chunks.extend([text[:137].encode(), text[137:450].encode(), text[450:].encode()])


def client_with(turns=2, callback=lambda *args: None):
    client = ConversationClient(callback); d = client.device
    d.connection = ConversationSerial(turns); d.trusted = d.held = True; d.last_heartbeat = time.monotonic()
    d.capture = {"samples": 64000, "bytes": 128000, "duration_ms": 4000, "worker_timeout_ms": 5500, "pcm_shift": 15}
    return client


class ConversationChecks(unittest.TestCase):
    def test_two_turns_and_quiet_share_one_connection(self):
        events = []; client = client_with(callback=lambda name, data: events.append((name, data)))
        result = client.capture("followup")
        self.assertEqual(result["completed_turns"], 2)
        self.assertTrue(result["same_connection"])
        self.assertEqual([c["after_turn"] for c in result["captures"]], [0, 1, 2])
        self.assertEqual(result["wake"]["cycle"], 3)
        self.assertEqual(client.device.connection.writes, [b"c", b"["])
        self.assertEqual([d["expect_speech"] for n, d in events if n == "recording"], [True, True, False])
        self.assertEqual(len([1 for n, _ in events if n == "microphone"]), 3)
        client.close(); self.assertFalse(client.device.connection.is_open)

    def test_silence_after_first_reply_sends_no_second_input(self):
        client = client_with(turns=1)
        result = client.capture("quiet")
        self.assertEqual(result["completed_turns"], 1)
        self.assertEqual(len(result["captures"]), 2)
        self.assertEqual(result["vad"][0]["accepted"], 0)
        client.close()

    def test_missing_close_partial_upload_and_duplicate_connection_fail(self):
        text = network_trace()
        for bad in [text.replace("close_ack=1", "close_ack=0"), text.replace("bytes=128000", "bytes=64000"),
                text.replace("stream_ended=1", "stream_ended=0"), text + "CompanionGemini: CONNECT_BEGIN",
                text.replace("played_samples=48000", "played_samples=47999"), text.replace("quiet_discards=1", "quiet_discards=0"),
                text.replace("accepted=0", "accepted=1")]:
            with self.subTest(trace=bad), self.assertRaises(CheckFailure): conversation_metadata(bad, 2)

    def test_single_turn_and_fixture_cannot_count_as_live_followup(self):
        with self.assertRaises(CheckFailure): conversation_metadata(network_trace(1), 2)
        with self.assertRaises(CheckFailure): conversation_metadata(network_trace(fixture=True), 2)
        self.assertTrue(conversation_metadata(network_trace(fixture=True), 2, fixture=True)["fixture"])

    def test_only_verified_board_and_conversation_commands(self):
        client = client_with(); d = client.device; d.trusted = False
        with self.assertRaises(CheckFailure): d.send(b"c")
        d.trusted = True
        for command in [b"a", b"q", b"f", b"b", b"n", b"v", b"~credential"]:
            with self.assertRaises(CheckFailure): d.send(command)
        self.assertEqual(d.connection.writes, [])

    def test_heartbeats_continue_after_wake_until_conversation_finishes(self):
        client = client_with(); d = client.device
        d.held = d.wake_waiting = False; d.conversation_owned = True; d.last_heartbeat = 0
        d.keepalive(); self.assertEqual(d.connection.writes, [b"="])
        d.conversation_owned = False; d.last_heartbeat = 0; d.keepalive()
        self.assertEqual(d.connection.writes, [b"="])

    def test_cancel_before_arm_and_sensitive_output_are_safe(self):
        client = client_with(); client.cancel.set()
        with self.assertRaises(TrialCancelled): client.capture()
        self.assertNotIn(b"c", client.device.connection.writes)
        d = client.device; d.connection.chunks = [b"transport ?access_token=private-fixture\n"]
        with self.assertRaises(CheckFailure) as error: d.read()
        self.assertNotIn("private-fixture", str(error.exception))

    def test_tk_quiet_full_review_save_retry_and_close_are_durable(self):
        with tempfile.TemporaryDirectory(prefix="esp32-conversation-ui-") as temp:
            window = tk.Tk(); app = ConversationApp(window, demo=True, results=Path(temp)); errors = []
            window.report_callback_exception = lambda *args: errors.append(args[1])
            def until(condition, timeout=10):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    window.update()
                    if condition(): break
                    time.sleep(.01)
                else: self.fail("Conversation UI did not reach expected state.")
                self.assertFalse(errors, errors)
            try:
                app.connect(); until(lambda: app.connected)
                self.assertEqual(app.data["attempts"], [])
                self.assertEqual(str(app.radios[1]['state']), "disabled")
                app.start(); until(lambda: app.status.get() == "Say Hi, E-S-P"); app.stop(); until(lambda: not app.busy)
                self.assertEqual(app.current["state"], "cancelled")
                app.start(); until(lambda: app.current["state"] == "awaiting_confirmation")
                self.assertEqual(len(app.current["captures"]), 2)
                self.assertEqual(app.current["measurement"]["completed_turns"], 1)
                app.feedback(True); self.assertEqual(app.exercise.get(), "followup")
                self.assertEqual(str(app.radios[1]['state']), "normal")
                with mock.patch("robot_conversation_test.atomic_json", side_effect=OSError()): app.save()
                app.refresh(); self.assertTrue(app.save_failed)
                self.assertEqual(str(app.arm_button['state']), "disabled")
                app.retry_save(); self.assertFalse(app.save_failed)
                app.start(); until(lambda: app.current["state"] == "awaiting_confirmation")
                self.assertEqual(len(app.current["turn_results"]), 2)
                self.assertEqual(len(app.current["captures"]), 3)
                app.feedback(True)
                window.update_idletasks()
                self.assertLessEqual(app.finish_button.winfo_rooty() + app.finish_button.winfo_height(), window.winfo_rooty() + window.winfo_height())
                app.start(); until(lambda: app.recording_started is not None); app.finish()
                until(lambda: json.loads((app.folder / 'session.json').read_text(encoding='utf-8'))['status'] == 'closed')
                saved = json.loads((app.folder / 'session.json').read_text(encoding='utf-8'))
                self.assertTrue(saved['demo']); self.assertFalse(saved['raw_audio_saved']); self.assertFalse(saved['transcript_saved'])
                self.assertEqual(saved['attempts'][1]['state'], 'verified_by_user')
                self.assertEqual(saved['attempts'][2]['state'], 'verified_by_user')
                self.assertIn(saved['attempts'][3]['state'], {'cancelled', 'interrupted'})
            finally:
                app.worker.closing.set(); app.worker.cancel.set(); app.worker.join(timeout=3)
                try: window.destroy()
                except tk.TclError: pass


if __name__ == '__main__': unittest.main()
