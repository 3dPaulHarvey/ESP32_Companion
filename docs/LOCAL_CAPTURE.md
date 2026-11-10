# Bounded local microphone capture

Phase 2b replaces the timed LISTENING rehearsal with a real, bounded PCM segment. It uses the existing audio input task and board/codec. The diagnostic profile still starts with Wi-Fi off and no cloud protocol.

Status: built and flashed on COM26. Automated capture/cancellation checks and three voice-triggered captures passed in serial. The first physical trial reported that the listening pose did not hold; the steady-pose/status-label correction is flashed but its physical retest is deferred at the user's request. Microphone gain/headroom calibration is also deferred. This is not yet a fully verified physical checkpoint.

## Interaction

1. In a quiet room, stand about 1–2 feet away and say **“Hi, E-S-P.”**
2. Look for **LISTENING** above the steady, left-shifted eyes. Briefly pause (about half a second), then say a short phrase such as **“Testing one two”**.
3. The robot holds that pose while collecting two seconds of audio, shows **CUE** with its existing end sound, then returns to blinking eyes and **READY**.
4. Wait for idle before repeating.

The cue marks the end of capture. This test reports audio measurements; it does not transcribe, replay or answer the phrase. The segment stays in RAM and is wiped/freed after measurement. Serial logs contain metadata, not raw PCM.

## Data and ownership

- Format: 16 kHz, mono, signed int16 PCM through the existing conversion.
- Maximum segment: **32,000 actual samples / 64,000 bytes / 2,000 ms**.
- Discard: the first 3,200 samples, excluded from both the segment and its statistics, to flush stale input from the wake transition.
- One reader: the existing `AudioInputTask`; capture never uses Opus send or replay queues.
- The input worker owns the PSRAM allocation and clears/frees it before publishing metadata. The UI receives no PCM pointer.
- Exact counts come from the finite-read codec API, including real partial reads. Zero-filled vector capacity is not counted as microphone data.
- Read wait: at most 100 ms per read. Worker watchdog: 3.5 seconds. Controller watchdog: 4 seconds.
- Success advances LISTENING to SPEAKING. Cancellation/error/timeout increments `aborted`, without claiming a completed cycle.
- Request IDs prevent a late old result from finishing a newer cycle. Readiness/new cycles remain blocked until cleanup is acknowledged.

Normal baseline reads retain their original wait and conversion. No GPIO, sample-rate or gain changes are made by this increment. Headroom/clipping calibration remains a separate audio-quality task.

## Commands

```powershell
.\tools\idf.ps1 -Action build -Profile diagnostics
.\tools\idf.ps1 -Action flash -Profile diagnostics -Port COM26
python .\tools\capture_serial.py --reset --send s --send-after 10 --seconds 20 --name capture-boot
python .\tools\test_local_cycles.py --cycles 3 --cancel --name capture-cancel-recovery
```

`n` starts a synthetic capture cycle. `x` cancels a pending capture while LISTENING. `s` reports success/abort counters and pending cleanup. Source labels distinguish serial tests from voice recognition. The host test first cancels one request, then verifies normal captures, buffer lengths, timing, busy-command guards, counters and return to IDLE.

Expected success evidence:

```text
LocalCompanion: STATE WAKE_DETECTED -> LISTENING cycle=1 source=voice
LocalCompanion: CAPTURE_BEGIN request=1 target_samples=32000 bytes=64000 sample_rate=16000 warmup_samples=3200
LocalCompanion: CAPTURE_COMPLETE request=1 samples=32000 bytes=64000 duration_ms=2000 sample_rate=16000 elapsed_ms=... rms=... peak=... clipped=...
LocalCompanion: STATE LISTENING -> SPEAKING cycle=1 source=voice
LocalCompanion: CYCLE_COMPLETE cycle=1 ...
HardwareDiagnostics: WAKE_READY ...
```

`elapsed_ms` includes discard and capture; it is distinct from the 2,000 ms of stored audio. Compare room quiet with normal speech using RMS/peak and clipped count. A successful count proves bounded capture, not intelligibility or perfect audio gain. Zero clipping is preferable for ordinary speech, but a tap/loud transient is a different condition.

Cancellation produces `CAPTURE_FAILED ... status=cancelled` and `CYCLE_ABORTED`, then re-arms after cleanup. At idle, `started = completed + aborted` and `pending=0`. If cleanup is still pending, the status says so instead of announcing wake readiness; use a hardware reset when diagnosing an unresponsive I2S path.

The portable C++ tests cover exact limits, short chunks, overshoot, discard exclusion, RMS/extrema/clipping, partial failures and buffer clearing. Controller tests cover capture completion, watchdog priority, cancellation and retry. See [TEST_LOG.md](TEST_LOG.md) for actual hardware results and [BUILD_AND_FLASH.md](BUILD_AND_FLASH.md) for baseline recovery.
