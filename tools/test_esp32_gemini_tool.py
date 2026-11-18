"""Offline result-contract checks; no real credentials, USB or network."""
import unittest
from check_esp32_gemini import CheckFailure, setup_metadata

VALID = "CompanionGemini: RESULT ok=1 upgrade_http=101 setup_ack=1 close_sent=1 close_ack=1 close_code=1000 ack_bytes=20 elapsed_ms=500 cleanup=1 no_question=1 no_audio_upload=1"


class GeminiHostChecks(unittest.TestCase):
    def test_complete_setup_and_close_is_accepted(self):
        self.assertEqual(setup_metadata(VALID)["upgrade_http"], 101)

    def test_upgrade_without_application_ack_cannot_pass(self):
        with self.assertRaises(CheckFailure):
            setup_metadata(VALID.replace("setup_ack=1", "setup_ack=0"))

    def test_application_ack_without_close_cannot_pass(self):
        with self.assertRaises(CheckFailure):
            setup_metadata(VALID.replace("close_ack=1", "close_ack=0"))

    def test_redirect_or_error_is_not_a_verified_upgrade(self):
        for status in [302, 401, 403]:
            with self.subTest(status=status), self.assertRaises(CheckFailure):
                setup_metadata(VALID.replace("upgrade_http=101", f"upgrade_http={status}"))

    def test_policy_close_and_oversized_ack_cannot_pass(self):
        for changed in [VALID.replace("close_code=1000", "close_code=1008"), VALID.replace("ack_bytes=20", "ack_bytes=16385")]:
            with self.assertRaises(CheckFailure):
                setup_metadata(changed)

    def test_missing_cleanup_or_audio_guards_cannot_pass(self):
        for marker in ["cleanup=1", "no_audio_upload=1", "no_question=1"]:
            with self.subTest(marker=marker), self.assertRaises(CheckFailure):
                setup_metadata(VALID.replace(marker, marker[:-1] + "0"))


if __name__ == "__main__":
    unittest.main()
