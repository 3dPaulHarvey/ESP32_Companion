"""Offline speaker-check result gates; no USB, network or credentials."""
import unittest
import contextlib
import io
from check_esp32_voice import CheckFailure, VoiceDevice, voice_metadata
from test_esp32_session_tool import FakeSerial, EXPECTED_MAC

VALID = ("CompanionVoice: RESULT ok=1 http=200 reason=verified upgrade_http=101 setup_ack=1 "
    "prompt_sent=1 turn_complete=1 close_sent=1 close_ack=1 close_code=1000 audio_bytes=96000 "
    "chunks=20 sample_rate=24000 played_samples=48000 duration_ms=2000 rms=1000 peak=5000 "
    "clipped=0 playback_ms=2030 elapsed_ms=6000 cleanup=1 no_audio_upload=1")


class VoiceHostChecks(unittest.TestCase):
    def test_real_serial_contract_with_guard_commands_and_release(self):
        device = VoiceDevice()
        trace = ("CompanionSession: REQUEST_BEGIN kind=voice\nCompanionGemini: CONNECT_BEGIN\n"
            "CompanionVoice: UI state=CONNECTING wake_armed=0\n"
            "CompanionVoice: REQUEST_REJECTED reason=local_busy\n"
            "LocalCompanion: WAKE_IGNORED source=serial state=IDLE\n"
            "LocalCompanion: COMMAND_IGNORED command=d state=IDLE\n"
            "LocalCompanion: COMMAND_IGNORED command=t state=IDLE\n"
            "CompanionSession: REQUEST_REJECTED reason=busy\n"
            "CompanionVoice: UI state=SPEAKING wake_armed=0\n" + VALID + "\n"
            "CompanionVoice: UI state=READY wake_armed=1\n")
        device.connection = FakeSerial(trace.encode(),
            f"CompanionSession: PROTOCOL version=1 mac={EXPECTED_MAC} configured=1 busy=0\n".encode(),
            b"CompanionVoice: STATUS active=0 speaking=0 wake_armed=1 playback_busy=0\n")
        with contextlib.redirect_stdout(io.StringIO()): device.check_voice(guards=True)
        self.assertEqual(device.voice_stage, "complete")
        self.assertTrue(device.checks[-1]["passed"])

    def test_complete_and_fully_played_turn_passes(self):
        self.assertEqual(voice_metadata(VALID)["played_samples"], 48000)

    def test_setup_and_every_completion_gate_are_required(self):
        for field in ["setup_ack", "prompt_sent", "turn_complete", "close_sent", "close_ack"]:
            with self.subTest(field=field), self.assertRaises(CheckFailure):
                voice_metadata(VALID.replace(field + "=1", field + "=0"))

    def test_partial_playback_cannot_pass(self):
        with self.assertRaises(CheckFailure):
            voice_metadata(VALID.replace("played_samples=48000", "played_samples=47999"))

    def test_empty_odd_or_oversized_pcm_cannot_pass(self):
        for size in [0, 95999, 480002]:
            with self.subTest(size=size), self.assertRaises(CheckFailure):
                voice_metadata(VALID.replace("audio_bytes=96000", f"audio_bytes={size}"))

    def test_fast_or_stalled_playback_cannot_pass(self):
        for timing in [200, 4000, 9000]:
            with self.subTest(timing=timing), self.assertRaises(CheckFailure):
                voice_metadata(VALID.replace("playback_ms=2030", f"playback_ms={timing}"))

    def test_wrong_rate_error_and_policy_close_cannot_pass(self):
        for changed in [VALID.replace("sample_rate=24000", "sample_rate=16000"),
            VALID.replace("close_code=1000", "close_code=1008"), VALID.replace("http=200", "http=503")]:
            with self.assertRaises(CheckFailure): voice_metadata(changed)

    def test_nonphysical_sample_metrics_cannot_pass(self):
        for changed in [VALID.replace("rms=1000", "rms=6000"), VALID.replace("clipped=0", "clipped=48001")]:
            with self.assertRaises(CheckFailure): voice_metadata(changed)

    def test_cleanup_and_no_upload_are_required(self):
        for marker in ["cleanup=1", "no_audio_upload=1"]:
            with self.subTest(marker=marker), self.assertRaises(CheckFailure):
                voice_metadata(VALID.replace(marker, marker[:-1] + "0"))


if __name__ == "__main__": unittest.main()
