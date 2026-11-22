# First Gemini speech through the robot

Phase 4c **passed on COM26 on 2026-11-17**. The development ESP32 receives a fixed Gemini greeting, closes its cloud connection, plays the complete reply through its existing speaker, and returns to READY. The user confirms clear speech at normal speed and the speaking expression/READY transition.

This is the speaker checkpoint within Phase 4. The robot's microphone has not been connected to Gemini. [Roadmap progress and next gate](ROADMAP_STATUS.md).

## Current behavior

The **voice** profile verified here uses **Hi, E-S-P -> LISTENING -> two-second local capture -> CUE -> READY**, with Wi-Fi off at boot. On 2026-11-18 COM26 advanced through [microphone calibration](MICROPHONE_CALIBRATOR.md) to the [manual question profile](ESP32_QUESTIONS.md), with four-second capture and reviewed headroom. The manual greeting below remains available outside calibration/question jobs.

For one cloud greeting, use USB serial:

1. Send `w` to connect using the saved Wi-Fi credentials.
2. Send `v`. The display shows CONNECTING while receiving the reply.
3. Watch SPEAKING and hear a short English greeting saying it is ready to chat.
4. The robot returns to READY and rearms local wake detection. Send `o` to stop Wi-Fi, or reset to its Wi-Fi-off startup.

The fixed text request is: "In one short sentence, say hello and tell me you are ready to chat." No microphone recording is sent. Wake detection pauses during this manual check so the speaker cannot trigger a new local cycle. Competing local capture/face/cue commands and duplicate cloud requests are rejected while it is active. The existing `g`, `b`, `u` and setup-only `j` commands remain available.

## Repeatable checks

Close other serial monitors, then run from the project root:

```powershell
.\tools\idf.ps1 -Action build -Profile voice
.\tools\idf.ps1 -Action flash -Profile voice -Port COM26
python .\tools\test_esp32_voice_tool.py
python .\tools\check_esp32_voice.py
```

The checker plays one greeting and verifies its exact sample count, playback timing, expressions, busy guards, offline local recovery and quiet reboot. Startup tones and the later local cue are separate from the spoken greeting. The checker leaves Wi-Fi off; listener confirmation is recorded separately.

For a bounded repeat/failure check:

```powershell
python .\tools\check_esp32_voice.py --repeats 2 --network-drop
```

This plays two greetings, then deliberately stops **device-side Wi-Fi** while another Gemini connection opens. That expected failure must release the job, restore READY/wake, and permit an offline local capture/cue cycle. It does not toggle the access point. The requests share the existing Worker admission limit.

Build output is isolated in `%LOCALAPPDATA%/ESP32_Companion/build-voice/`, including its generated `sdkconfig`. The flash guard verifies COM26 / MAC `44:bd:8d:ec:38:e0`; normal flashing retains NVS. The previous `-Profile gemini`, `-Profile session` and local diagnostics profiles remain available. No new secret, cloud deployment or dependency is required.

## Data and playback

After the established temporary-token setup, the client sends only the fixed text request and receives raw 24 kHz, mono, signed 16-bit little-endian PCM. It requires `turnComplete` with nonempty audio and a normal close acknowledgment before starting the speaker. `generationComplete` alone cannot finish the turn. [Google's server-content fields](https://aistudio.google.com/api-reference/live#BidiGenerateContentServerContent).

The first version deliberately buffers the whole short reply in PSRAM. Its limits are 10 seconds / 480,000 PCM bytes, 64 KiB per WebSocket message, 256 response messages / 2 MiB aggregate, and a JSON tree of at most 192 tokens and eight nesting levels. Borrowed JSON tokens avoid allocating copies of base64 strings. Duplicate/escaped field names, malformed JSON/base64, wrong audio rate, odd PCM lengths, interruption, errors and unexpected tool messages fail the check. Bounded usage and session-resumption notices are ignored; no resumption handle is saved or used.

The Worker HTTP client is released before Gemini TLS is allocated; Gemini is closed before playback. The existing **AudioOutputTask** remains the only speaker writer. Its PCM queue holds at most two 20 ms chunks plus the chunk being written. Each diagnostic I2S write has a finite 250 ms wait and reports actual written samples. Playback reserves the output path, rejects overlapping cues, and waits for the last in-flight write rather than only an empty queue. The new path uses the original slot conversion and saved volume at **half amplitude** for this first speech test; it does not change the saved volume.

An 80 ms silence tail exceeds the board's 60 ms speaker-DMA capacity. A further 150 ms settling delay precedes wake rearming. Network work and playback waits run outside the display loop. Failures return control to local READY/wake without automatic retry. Existing certificate/name/date checks and compiled-out credential-bearing transport logs remain enforced. Owned PSRAM reply/transport buffers are zeroized and queued PCM is cleared; temporary codec/SDK allocations are normally freed. No audio, transcript, credential-bearing URL or raw serial stream is saved by the checker.

## Observed results

| Final greeting | Received PCM | Samples played | Audio duration | Playback/settling time | Complete request time |
|---|---:|---:|---:|---:|---:|
| Listener replay | 91,680 bytes | 45,840 | 1.910 s | 2.079 s | 9.868 s |
| Repeat 1 | 82,080 bytes | 41,040 | 1.710 s | 1.879 s | 10.078 s |
| Repeat 2 | 127,682 bytes | 63,841 | 2.660 s | 2.839 s | 12.250 s |

All three final checked greetings verify HTTP 200/101, setup, exactly one prompt, turn completion, close code 1000, exact playback count and READY/wake recovery. Each has zero clipped source samples. The user confirms the listener replay. The final repeat also passes busy-command guards, device-side Wi-Fi-loss recovery, offline capture/cue and quiet reboot. These are bounded checks, not an extended audio/network soak.

The build passes **72 compile-time assertions** (24 session, 16 framing/setup, 32 nested-JSON/base64/audio checks). All **22 host tests** pass (seven session, six setup, nine voice). The preceding Gemini profile rebuild passes with voice disabled. Detailed history, memory observations and the earlier parser/checker corrections are recorded in [TEST_LOG.md](TEST_LOG.md).

Application: 2,572,480 bytes, 16% app-partition space free; SHA256 `86bc5d34032b3bae0f13819888441fd0f833af5b6d88fde98a01de46dbb6340f`. Private pre-change recovery image: `recovery/2026-11-17-phase-4c/before-voice-full-flash.bin`, 8,388,608 bytes, SHA256 `8f042c9c2ae50ae9ee02a316cc63336db8d34fb9a10b82a732f1580c7a055d34`. The original recovery image remains intact. The tested voice write files, generated configuration and checksum manifest are also archived in that directory's `built-voice/` subdirectory.

Evidence is under ignored `logs/esp32-voice-*`, particularly `preflash.json`, `build-metadata-fix.log`, `flash-metadata-fix.log`, `listener-replay.log`, `final-hardware-verified.log`, `host-tests.json` and `physical-followup.json` with that prefix. Worker code/deployment is unchanged.

Next verify microphone headroom and a capture window long enough for a question, then send one controlled microphone question and play its answer. Wake-triggered cloud conversation, follow-ups and idle-session handling belong to the later integration gate.
