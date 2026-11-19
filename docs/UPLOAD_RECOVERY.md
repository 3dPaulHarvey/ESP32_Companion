# Bounded recovery of upload writes

The standalone upload path now resumes short TLS writes and temporary no-progress / WANT_READ / WANT_WRITE results within the existing frame budget. It continues the same frame's remaining bytes, preserves their order and never retries the question or opens a replacement Gemini connection automatically. A fatal error, expired budget or cancellation aborts that stream; a fresh wake can start a new conversation.

## What the earlier failure establishes

The reviewed standalone session recorded one `audio_send_failed` attempt after HTTP 200 and WebSocket upgrade 101, before any completed answer. Later fresh wakes succeeded. The old report did not retain byte progress or the native socket/TLS result, so the exact historical cause cannot be reconstructed. The source session and its earlier review remain unchanged.

Five additional back-to-back synthetic conversations passed with the original send timing and new instrumentation. Thus the original event was not reproduced in those baseline sessions; it does not establish an ongoing failure of the working robot.

Source inspection of the installed ESP-IDF **5.5.2** shows the relevant gap: `transport_ws.c::_ws_write` writes the WebSocket header and payload separately, requires the full header count and returns the payload write count to our caller. The underlying TLS API permits short writes and temporary WANT_READ/WANT_WRITE results. The previous conversation sender treated any non-full result as `audio_send_failed`. Resending the whole frame would risk inserting a second header inside an unfinished payload.

## Change and limits

The conversation client keeps the SDK's WebSocket framing, masking, read parser and strict verified TLS connection. A small transport adapter between WebSocket and TLS completes each byte sequence with an absolute deadline. It advances only after a positive accepted-byte count and repeats the same unwritten suffix after a temporary result. It checks cancellation, Wi-Fi and session lifetime between attempts. Native TLS/socket calls retain their own I/O timeouts; the adapter does not extend its deadline when a write stalls.

On failure the writer marks the stream unusable and releases it. It does not append a close frame to an incomplete frame, retry the failed audio question, or reconnect automatically. Cleanup failures are recorded separately, preserving the original failure reason. A healthy stream may still complete its bounded close handshake after cancellation. Explicit fresh wakes retain the existing failure cooldown and server Retry-After behavior.

Explicit diagnostic **o** during a conversation now cancels the job and waits for its TLS owner to finish before stopping the Wi-Fi driver. This removes concurrent full-driver teardown from an active TLS session. Ordinary link loss still follows the bounded transport-failure path and requires a fresh conversation after reconnection.

Only the bounded conversation transport changes. Wake detection, microphone gain/window, VAD, eyes, speaker timing, two-answer limit, Worker, Google model, credentials and TLS verification stay as before. The earlier one-question reference path remains separate.

## Diagnostics and repeatable checks

Numeric upload summaries contain accepted input bytes/chunks, EOF state, elapsed upload time, maximum send time and slow-send count. A failed send records requested/returned length, input progress, numeric socket/TLS status, Wi-Fi/RSSI and free internal memory. Writer totals show temporary waits and short writes. The self-paced follow-up and standalone apps autosave available diagnostics **before** the final result, so a failed upload retains its evidence. Cleanup reason remains separate. No audio, transcript, credential, temporary URL or raw server message is saved.

The developer synthetic image disables automatic cloud wake. Its first public-audio frame deliberately exercises four conditions:

1. A no-progress return before writing the header.
2. A short header write, emitting three real TLS payload bytes first.
3. A short audio-payload write, emitting seventeen real TLS payload bytes first.
4. A temporary WANT_WRITE result before continuing the remaining payload.

These faults occur once per synthetic conversation, under `COMPANION_QUESTION_FIXTURE` only. The normal image excludes the public test PCM, f handler and all fault-injection strings. Successful server replies, exact 128,000-byte / 100-frame inputs, EOF, quiet discard and acknowledged close demonstrate that the resumed byte stream remains valid.

```powershell
$taskBuildRoot = Join-Path $env:LOCALAPPDATA 'ESP32_Companion'
.\tools\idf.ps1 -Action build -Profile standalone -BuildRoot $taskBuildRoot -FixtureCheck
python .\tools\check_standalone_build.py --fixture
.\tools\idf.ps1 -Action flash -Profile standalone -BuildRoot $taskBuildRoot -Port COM26 -FixtureCheck
python .\tools\check_upload_recovery.py --name upload-recovery-repeat --cycles 10 --require-recovery
```

Use a new result name for each run; existing evidence is not overwritten. The CLI saves each completed/failed row before enforcing its pass gates. Prepare the established public fixtures with `prepare_followup_fixture.py` if absent. Restore normal firmware after testing:

```powershell
.\tools\idf.ps1 -Action build -Profile standalone -BuildRoot $taskBuildRoot
python .\tools\check_standalone_build.py
.\tools\idf.ps1 -Action flash -Profile standalone -BuildRoot $taskBuildRoot -Port COM26
```

Sixteen additional compiled write-policy assertions cover exact bytes/order after partial writes, zero progress, both WANT directions, mixed stalls, fatal errors before/after progress, cancellation before/after progress, deadlines including late completion, oversized return values and null/empty input. The standalone profile now compiles **155 assertions**. All **84 host/app tests** pass, including split serial diagnostics and autosave before a failed upload.

## Hardware results

| Check | Observed result |
|---|---|
| Instrumented original send path | Five consecutive synthetic conversations pass; original intermittent failure does not recur |
| Controlled write recovery | Ten consecutive synthetic conversations pass with all four fault conditions injected once in each session |
| Exact transfer and playback | Twenty complete inputs / 2,560,000 PCM bytes / 2,000 audio frames + EOF; twenty replies / 1,443,132 played samples, zero output clipping |
| Recovery counters | Twenty temporary waits and twenty positive short writes recovered; no question retries or duplicate connections |
| Quiet and closure | Ten zero-filled quiet segments discarded locally; every session reaches READY with acknowledged close 1000 |
| Upload timing | 5.052-6.695 s per input; maximum individual frame send 89 ms, including injected stalls |
| Controlled driver shutdown after guard | Cancelled after one complete input frame / 1,280 bytes; original cancellation reason preserved, close acknowledged at 1000, then Wi-Fi stops; one new explicit session completes both answers |
| Ordinary link loss after guard | Aborts before a reply with `websocket_read_failed`; retains partial input progress, TLS status 32797 / native code 76 and separate cleanup failure; reconnect plus a new explicit session completes both answers |

The first full-driver-stop test produced a device-fault/reset indication. Its native fault type and backtrace were not retained, so the precise reset cause is unproven. Two immediate repeats before the guard returned `wifi_lost` and recovered on fresh sessions. The explicit stop path was changed to release the conversation's TLS owner before driver teardown; post-change cancellation/close/shutdown and ordinary link-loss recovery both pass. This is a scoped ownership fix, not evidence that every possible reset or external outage is solved.

The ten-cycle core transport test used diagnostic SHA256 `d6f9c00c8d6de91a9584af9bfc3957da42abe18e6c124eef7e4cff09d39cfa88`. Subsequent shutdown/link-loss tests used the final diagnostic image `9b3e4e2d1a44b391166dbad515836a33f3858136b925d5ba62149a350a4b0d39`. The byte-resume implementation is the same; the later changes add shutdown ordering and permit bounded close after cancellation. Exact diagnostic files and symbols are archived separately.

Final normal app: **2,599,264 bytes**, SHA256 `091c6f19f39c2faa4cabaf60292f5566a056036c4ede4d099de84be752824b33`, archived with exact write files, configuration, symbols and hash manifest under `recovery/2026-11-18-upload-reliability/built-upload-final/`. Normal-image preflash checks exclude both public PCM fixtures, the f handler and fault-injection strings; preserve NVS; retain TLS CA/name/date checks; compile private logging out of five transport units; and find no actual device credential in the image.

The private pre-fix full flash is **8,388,608 bytes**, SHA256 `b63891eedd0792b75ae865592d7aec7eef4db07cc825725c5401c5524cf949f3`; its application matches the human-verified standalone image `90c02774...` byte-for-byte. Earlier backups and the user's source session remain unchanged. No unattended room microphone audio was uploaded during this investigation.

All **eight final normal-image startup/control checks pass** after guarded flashing: saved Wi-Fi with zero provider requests at boot, fixture exclusion, diagnostic pause/renewal/release, lost-USB lease recovery, manual offline preference, local calibrator capture/release and read-only observer connection/close. The robot is left online, idle, wake armed, unpaused and with serial closed. No room microphone audio was uploaded.

Evidence under ignored `logs/`: `esp32-upload-baseline-fixtures.json`, `esp32-upload-recovery-ten-cycles.json`, `esp32-upload-guarded-shutdown.json`, `esp32-upload-link-loss.json`, the original and repeated driver-stop reports, `robot-upload-recovery-host-tests.log`, final build/flash logs, `esp32-upload-normal-hardware-qa.json` and the derived `esp32-upload-investigation.json`.

Long idle, actual access-point outages and broader repeated real-voice reliability remain separate checks. Passing controlled write recovery does not identify the unrecorded native cause of the earlier event or guarantee success during a network outage.
