# Prerecorded audio question and answer

Phase 3f **passed on 2026-11-17**: a synthetic 16 kHz recording asked "What is two plus two?" Gemini returned a spoken answer of four at 24 kHz. Windows played both files, and the user confirmed the correct answer and clear speech at normal speed.

This proves the desktop audio-input/output exchange with the existing Worker-issued temporary token. No microphone was opened, no text copy of the question was sent to Gemini, and robot firmware was not changed. The next checkpoint is an authenticated session request from the development ESP32.

## Verified exchange

The fixed question is generated locally with Windows System.Speech and an installed English voice. Its complete utterance is preserved, with 80 ms of silence before it and 320 ms after it. The client checks its known question/source/format, byte count and SHA256 before requesting a token.

After `setupComplete`, the client sends only raw signed 16-bit little-endian mono PCM as `realtimeInput.audio`, with MIME type `audio/pcm;rate=16000`. Frames contain up to 40 ms (1,280 bytes) and are paced at the audio rate. An `audioStreamEnd` event follows the last frame to flush the default automatic voice detector. The WAV header and the local question text are not sent. [Google audio format and stream-end guidance](https://aistudio.google.com/docs/live-api/capabilities#send-receive-audio).

| Measurement | Question | Gemini answer |
|---|---|---|
| Rate / format | 16 kHz, mono, 16-bit PCM | 24 kHz, mono, 16-bit PCM |
| Frames | 38,560 | 102,720 |
| Duration | 2.410 seconds | 4.280 seconds |
| PCM bytes | 77,120 | 205,440 |
| Peak magnitude | 15,095 | 22,845 |
| RMS | 1,181.28 | 3,316.68 |
| Clipped samples | 0 | 0 |

Gemini's output transcript begins **"Two plus two equals four!"** The checker verifies the complete input byte count, stream-end event and expected answer in the bounded output transcript. The user separately confirmed the audible question and correct reply. The fixture is 2.41 seconds; this does not verify the robot's current two-second microphone capture or its microphone levels.

The input is bounded to six seconds, the output to 20 seconds, and the complete turn to 30 seconds. Socket closure or excessive send buffering stops input. The client rejects invalid output formats, malformed base64, unexpected tools/interruption, empty audio turns and oversized transcripts. Permanent/provider credentials are not written to audio or metadata files.

## Repeat the checkpoint

From the project root:

```powershell
.\tools\worker.ps1 -Action audio-fixture
.\tools\worker.ps1 -Action test-desktop
.\tools\worker.ps1 -Action audio-check
```

`audio-fixture` uses Windows speech synthesis locally and writes the WAV/raw PCM/identity manifest under ignored `logs/gemini-input-question.*`. It does not make a Google request or use a microphone. `test-desktop` runs six offline client tests with public fixtures and a fake socket. `audio-check` makes one real token/session request and uploads only the prepared synthetic question. It saves the reply WAV, bounded output transcript and controlled status metadata under `logs/worker-gemini-audio-check.*`.

To replay the existing question and answer without another provider request:

```powershell
$roundtripPlayer = New-Object System.Media.SoundPlayer
try {
    foreach ($clipName in @('gemini-input-question.wav', 'worker-gemini-audio-check.wav')) {
        $roundtripPlayer.SoundLocation = Join-Path (Get-Location) ('logs\' + $clipName)
        $roundtripPlayer.Load()
        $roundtripPlayer.PlaySync()
        Start-Sleep -Milliseconds 400
    }
} finally {
    $roundtripPlayer.Dispose()
}
```

The offline tests verify exact PCM reconstruction from the sent frames, format/pacing/stream-end, absence of text fallback, backpressure, stopping after mid-input closure, empty-output rejection and compatibility of setup-only/text-greeting modes. They use no real credentials or network. Worker code, bindings, dependency versions and hosted deployment are unchanged; the 20 Worker tests last passed in Phase 3d.

Evidence: `logs/gemini-input-question.json`, `worker-gemini-audio-check.log`, `worker-gemini-audio-check.transcript.txt`, `worker-gemini-audio-wave-validation.log` and `worker-gemini-audio-listener-check.log`. The six offline test results are recorded in `worker-gemini-audio-desktop-tests.log`. Generated WAV/PCM/transcript files remain outside Git.

Authenticated Worker session access on COM26 has now passed with CA/hostname/date validation, private credential provisioning and local-cycle recovery. [ESP32 session results](ESP32_SESSIONS.md). Next add direct Gemini WebSocket setup and embedded audio exchange. Microphone headroom and capture duration remain open before live robot conversation.
