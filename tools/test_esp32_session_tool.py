"""Offline checks for identity, privacy and result gates; no USB/network access."""

import contextlib
import io
import unittest

from check_esp32_session import CheckFailure, Device, EXPECTED_MAC, capture_policy


class FakeSerial:
    def __init__(self, *chunks):
        self.chunks = list(chunks)
        self.writes = []

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, _count):
        return self.chunks.pop(0) if self.chunks else b""

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        pass


def device_with(*chunks):
    device = Device()
    device.connection = FakeSerial(*chunks)
    return device


class SessionHostChecks(unittest.TestCase):
    def test_old_profile_retains_two_second_capture_contract(self):
        self.assertEqual(capture_policy("")["samples"], 32000)

    def test_new_profile_adopts_valid_four_second_capture_policy(self):
        device = device_with(("LocalCompanion: CAPTURE_POLICY samples=64000 bytes=128000 duration_ms=4000 sample_rate=16000 worker_timeout_ms=5500 pcm_shift=14\n"
            f"CompanionSession: PROTOCOL version=1 mac={EXPECTED_MAC} configured=1 busy=0\n").encode())
        self.assertEqual(device.protocol(), (1, 0))
        self.assertEqual(device.capture["samples"], 64000)

    def test_inconsistent_or_excessive_capture_policy_is_rejected(self):
        fixture = "LocalCompanion: CAPTURE_POLICY samples=64000 bytes=128000 duration_ms=4000 sample_rate=16000 worker_timeout_ms=5500 pcm_shift=14"
        for changed in [fixture.replace("bytes=128000", "bytes=128002"), fixture.replace("pcm_shift=14", "pcm_shift=32"), fixture.replace("duration_ms=4000", "duration_ms=2000")]:
            with self.assertRaises(CheckFailure): capture_policy(changed)

    def test_four_second_capture_exact_count_and_cycle_are_required(self):
        device = device_with(b"LocalCompanion: CAPTURE_COMPLETE request=2 samples=64000 bytes=128000 duration_ms=4000 sample_rate=16000 elapsed_ms=4120 rms=1000 peak=5000 clipped=0\n"
            b"LocalCompanion: CYCLE_COMPLETE cycle=2 completed=2 source=serial voice_started=0 serial_started=2 aborted=0\n")
        device.capture = capture_policy("LocalCompanion: CAPTURE_POLICY samples=64000 bytes=128000 duration_ms=4000 sample_rate=16000 worker_timeout_ms=5500 pcm_shift=14")
        self.assertEqual(device.wait_local_capture(0)["samples"], 64000)

    def test_wrong_board_is_rejected_after_only_a_status_command(self):
        device = device_with(b"CompanionSession: PROTOCOL version=1 mac=44:bd:8d:ed:4e:4c configured=0 busy=0\n")
        with self.assertRaises(CheckFailure):
            device.protocol()
        self.assertEqual(device.connection.writes, [b"s"])

    def test_correct_identity_and_heap_metadata_are_accepted(self):
        device = device_with(("HardwareDiagnostics: HEAP internal_free=80000 internal_min=70000 internal_largest=60000 psram_total=8388608 psram_free=6000000\n"
            f"CompanionSession: PROTOCOL version=1 mac={EXPECTED_MAC} configured=1 busy=0\n").encode())
        self.assertEqual(device.protocol(), (1, 0))
        self.assertEqual(device.heaps[0]["internal_free"], 80000)

    def test_a_credential_echo_across_reads_is_suppressed(self):
        fixture = "0123456789abcdef" * 4  # Public fixture, never a real key.
        device = device_with(fixture[:20].encode(), fixture[20:].encode())
        device.credential = fixture
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            device.read()
            with self.assertRaises(CheckFailure) as error:
                device.read()
        self.assertNotIn(fixture, str(error.exception))
        self.assertEqual(output.getvalue(), "")

    def test_a_panic_is_rejected_without_copying_raw_output(self):
        device = device_with(b"Guru Meditation: arbitrary-private-data\n")
        with self.assertRaises(CheckFailure) as error:
            device.read()
        self.assertNotIn("arbitrary-private-data", str(error.exception))

    def test_a_credential_bearing_transport_url_is_suppressed(self):
        device = device_with(b"SDK error request ?access_token=public-fixture\n")
        with self.assertRaises(CheckFailure) as error:
            device.read()
        self.assertNotIn("public-fixture", str(error.exception))

    def test_serial_output_is_bounded(self):
        device = device_with(b"x" * 500001)
        with self.assertRaises(CheckFailure):
            device.read()

    def test_oversized_temporary_credential_metadata_cannot_pass(self):
        device = device_with(b"CompanionSession: RESULT kind=session ok=1 http=200 reason=verified temporary_bytes=8193 start_in_s=59 expires_in_s=299 elapsed_ms=100 discarded=1\n")
        with self.assertRaises(CheckFailure):
            device.probe(b"g", "session", 200, True)
        self.assertEqual(device.checks, [])


if __name__ == "__main__":
    unittest.main()
