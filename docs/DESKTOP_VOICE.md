# First desktop spoken reply

Phase 3e's first output checkpoint **passed on 2026-11-17**. Using the verified Worker-issued temporary token, Gemini answered a fixed text request with a short audio reply. Windows played it through the PC speakers, and the user confirmed **clear speech at normal speed**.

This proves text-to-spoken-output generation, PCM reception, WAV packaging and desktop playback. Audio input and ESP32 transport remain the next checks. The robot's firmware and local wake/listen/cue behavior were not changed.

## What was sent and received

The desktop client sends this fixed request after `setupComplete`:

> In one short sentence, say hello and tell me you are ready to chat.

No microphone is opened. The client reuses the same short-lived, one-use Gemini session policy. It accepts only 24 kHz signed 16-bit little-endian mono PCM, receives until `turnComplete`, then closes the connection. Async decoding of binary JSON messages is serialized to preserve chunk order. Unexpected tools, interruption, invalid base64, a different audio format, more than 20 seconds of PCM or a 30-second turn timeout fail the check.

| Measurement | Result |
|---|---|
| Duration | 2.74 seconds |
| Frames | 65,760 |
| Format | 24,000 Hz, 1 channel, 16-bit PCM |
| Peak magnitude | 25,382 / 32,768 |
| RMS | 4,694.11 |
| Clipped samples | 0 |
| Windows playback | Completed successfully |
| Listener confirmation | Clear speech, normal speed |

## Repeat or replay

Generate a new reply from the project root:

```powershell
.\tools\worker.ps1 -Action voice-check
```

This makes one real token/session request and sends the fixed text question to Google. It writes an ignored local WAV and controlled metadata under `logs/worker-gemini-voice-check.*`. It does not automatically play the file. No permanent key or temporary token is saved there.

To replay the existing file without another Google request:

```powershell
$replyPath = Join-Path (Get-Location) 'logs\worker-gemini-voice-check.wav'
$replyPlayer = New-Object System.Media.SoundPlayer
try {
    $replyPlayer.SoundLocation = $replyPath
    $replyPlayer.Load()
    $replyPlayer.PlaySync()
} finally {
    $replyPlayer.Dispose()
}
```

The existing `session-check` command still performs only the setup acknowledgment and sends no question/audio. Its hosted regression passed after adding voice mode. Worker code, bindings and the deployment from [GEMINI_SESSIONS.md](GEMINI_SESSIONS.md) were not changed by this desktop increment.

Evidence: `logs/worker-gemini-voice-check.log`, `worker-gemini-voice-check.wav`, `worker-gemini-voice-wave-validation.log`, and `worker-gemini-voice-listener-check.log`. Node/PowerShell syntax and whitespace checks pass; Python's independent WAV reader verified the generated file's format, duration and sample statistics. No provider call is needed to repeat that file inspection.

The next prerecorded 16 kHz question / 24 kHz answer checkpoint has now passed, including user-confirmed playback. [Audio exchange results](DESKTOP_AUDIO.md). Next prove authenticated Worker/session access on COM26, then integrate embedded audio transport and finish microphone checks.
