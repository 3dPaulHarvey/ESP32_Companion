"""Offline raw-headroom/window contract tests; no USB, network or recordings."""
import unittest
from check_esp32_microphone import CheckFailure, microphone_metadata

POLICY = {"pcm_shift": 14}
CAPTURE = {"request": 3, "samples": 64000, "bytes": 128000, "duration_ms": 4000,
    "elapsed_ms": 4120, "rms": 1000, "peak": 16384, "clipped": 0}
HEADROOM = ("MicrophonePrep: HEADROOM request=3 samples=64000 pcm_shift=14 raw_peak=268435456 "
    "legacy_clipped=100 raw_near_full_scale=0 suggested_shift=14 dc_mean=0 ac_rms=1000\n")
WINDOWS = "".join(f"MicrophonePrep: WINDOW request=3 second={index} samples=16000 rms=1000 peak=16384 clipped=0\n" for index in range(1,5))


class MicrophoneHostChecks(unittest.TestCase):
    def test_complete_metadata_is_accepted(self):
        result = microphone_metadata(CAPTURE, HEADROOM + WINDOWS, POLICY)
        self.assertEqual(result["legacy_clipped"], 100)
        self.assertEqual(len(result["windows"]), 4)

    def test_warmup_cannot_be_counted_as_stored_raw_samples(self):
        with self.assertRaises(CheckFailure): microphone_metadata(CAPTURE, HEADROOM.replace("samples=64000", "samples=67200") + WINDOWS, POLICY)

    def test_shift_and_recommendation_must_match(self):
        for changed in [HEADROOM.replace("pcm_shift=14", "pcm_shift=12"), HEADROOM.replace("suggested_shift=14", "suggested_shift=13")]:
            with self.assertRaises(CheckFailure): microphone_metadata(CAPTURE, changed + WINDOWS, POLICY)

    def test_windows_cannot_be_missing_or_repeated(self):
        for changed in [WINDOWS.splitlines()[0] + "\n", WINDOWS.replace("second=4", "second=3")]:
            with self.assertRaises(CheckFailure): microphone_metadata(CAPTURE, HEADROOM + changed, POLICY)

    def test_window_peak_and_clip_totals_must_match(self):
        for changed in [WINDOWS.replace("peak=16384", "peak=10000"), WINDOWS.replace("clipped=0", "clipped=1")]:
            with self.assertRaises(CheckFailure): microphone_metadata(CAPTURE, HEADROOM + changed, POLICY)

    def test_nonphysical_dc_ac_and_raw_metrics_are_rejected(self):
        for changed in [HEADROOM.replace("dc_mean=0", "dc_mean=2000"), HEADROOM.replace("ac_rms=1000", "ac_rms=2000"),
            HEADROOM.replace("raw_peak=268435456", "raw_peak=4294967295")]:
            with self.assertRaises(CheckFailure): microphone_metadata(CAPTURE, changed + WINDOWS, POLICY)

    def test_window_rms_must_match_aggregate(self):
        with self.assertRaises(CheckFailure): microphone_metadata(CAPTURE, HEADROOM + WINDOWS.replace("rms=1000", "rms=500"), POLICY)


if __name__ == "__main__": unittest.main()
