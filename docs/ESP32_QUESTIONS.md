# First manual microphone question

**Phase 4e passed on 2026-11-18.** A real microphone question received the correct spoken Gemini answer through COM26's speaker and returned to READY. The saved result verifies complete upload, output, closure and playback; the user confirms the correct spoken answer. COM26 runs the **question** profile with reviewed microphone settings. Wake-triggered cloud conversation is the next integration step.

## Repeat the manual test

1. Connect the development robot on COM26 and close serial monitors. Open **Robot Spoken Question Test** on the Desktop, or **Open Robot Question Test.cmd** in the project.
2. Click **Connect robot**. The app verifies the development MAC, checks capture shift 15, connects saved Wi-Fi, and completes a Gemini setup/close check before enabling Start. This preflight sends no question or microphone audio. It then pauses incidental wake recognition between samples.
3. From 1–2 feet away in a quiet room, click **Start question**. Wait for the countdown and green **SPEAK NOW**, then say **“What is two plus two?”** once at normal volume.
4. Stop at the local end cue. The robot shows CONNECTING, receives a reply, shows SPEAKING while playing it, then returns to READY.
5. Choose **Yes — clear answer: four** only if you heard a correct answer at normal speed and saw READY. Otherwise select **Answer or audio problem**. Click **Finish & close** and tell Codex the test is finished.

Each Start authorizes exactly one four-second microphone capture to be sent to Gemini. It sends the captured PCM, without a text version of the question. No audio recording or transcript is saved on the PC. Connection alone, normal wake, and the microphone calibrator never start cloud questions. Closing normally cancels pending work, restores wake, releases COM26 and turns the robot's Wi-Fi off. A reply already playing may finish while cleanup waits; it is bounded to ten seconds. A disconnected USB cable may prevent host cleanup; resetting the robot returns to its Wi-Fi-off startup.

Every attempt and its available numeric microphone/network/playback result is autosaved under `logs/robot-question-tests/<session id>/session.json`; `latest-session.json` points to the newest run. Microphone measurements are saved as soon as capture completes, including when a later cloud step fails. The user's physical confirmation is separate from transport success. Partial/cancelled runs remain available. If saving fails, the app preserves the prior JSON and offers Retry save. Demo/test results live in separate QA folders and cannot establish a real pass.

## Applied microphone measurements

The accepted 2026-11-18 calibration contains three quiet and three normal-speech captures. All six have exact four-second counts; all three speech samples were confirmed to fit the window. At capture shift 14, speech had **zero clipped samples**, a maximum peak of **17,049 / 32,768**, and a median active-speech/quiet **level contrast of 12.72 dB**. The same raw input would have hit the old shift-12 clipping limit 2,067 times. No near-full-scale raw words were reported. The optional wake test was skipped; earlier physical wake verification remains valid.

Capture shift **15** is configured in both microphone and question profiles to preserve the chosen minimum 6 dB peak-headroom target across all accepted raw peaks. The original prediction was about **8,525 / 32,768** at that gain. The successful live question subsequently measured **9,035 / 32,768**, with zero clipping, and established intelligibility at the applied gain. Wake recognition retains shift **12**. Duration stays **four seconds**, with no new noise gate or filter.

Original session: ignored `logs/microphone-calibration/2026-11-18_12-20-12_18333abe/session.json`, SHA256 `488860de012e4eedd394ceaa223824c2cc23f58bc6368ccb1be87694c404636f`. Its measured policy remains unchanged at 14; the applied decision is recorded separately in `logs/microphone-calibration-applied.json`.

## Firmware ownership and limits

Only explicit serial **q** can retain a question capture. The existing input task remains the sole microphone reader. It transfers the successful 128,000-byte allocation directly to the session task; the UI receives only metadata. Normal wake/calibration PCM is still wiped before metadata publication. Failed, cancelled, stale, rejected and pre-upload network-loss paths clear retained input.

After the end cue, the session task obtains a fresh temporary token through the existing Worker, opens the verified constrained Gemini WebSocket, and sends **100 PCM frames of 640 samples / 40 ms**, followed by `audioStreamEnd`. Incoming messages and ping/close controls are processed during upload. Input and base64 workspaces are wiped; the input allocation is released immediately after EOF, or on any earlier failure. This uses the existing server-side activity detection and EOF signal. [Google's realtime input fields](https://googleapis.github.io/js-genai/release_docs/interfaces/types.LiveClientRealtimeInput.html).

The established reply validator still enforces 24 kHz mono PCM, complete nonempty output, normal closure, a ten-second/480,000-byte audio bound, and exact playback counts. Documented voice-activity notices are ignored as bounded metadata. Audio-question input transcription is type-checked and discarded without logging or saving its text; it does not finish a turn or append playable audio. The fixed-greeting mode still rejects unexpected input transcription. [Google's server message fields](https://aistudio.google.com/api-reference/live#BidiGenerateContentServerMessage).

The cloud connection closes before playback. The output task remains the sole speaker writer, and wake waits for settling. Setup, upload, reply, closure and playback all have finite deadlines. Every temporary-session request requires a fresh bounded time synchronization; a plausible RTC date alone is insufficient for a one-minute start deadline. A failed request never retries automatically. Worker code, key storage, dependency versions and TLS checks are unchanged.

```powershell
.\tools\idf.ps1 -Action build -Profile question
.\tools\idf.ps1 -Action flash -Profile question -Port COM26
python .\tools\test_robot_question.py
```

The flash wrapper verifies the development MAC. `question` uses separate `build-question/` and `sdkconfig.question.generated`; `microphone` remains the local-only alternative. Both now seed shift 15. Existing generated configurations are not overwritten by default files; the local microphone configuration was explicitly updated before its regression build.

Initial local-only hardware checks verified offline/held-control rejection, early cancellation, busy guards, retained-input discard when Wi-Fi disappears before upload, subsequent ordinary capture, and idle/wake recovery. Those initial checks made **no provider request and uploaded no room audio**. Subsequent prerecorded and live results are recorded below. [Roadmap](ROADMAP_STATUS.md).

## Verified live result

The user retested, selected the clear/correct-answer confirmation, closed the app, and confirmed in chat that the spoken question received the correct spoken result. The closed, non-demo session `2026-11-18_14-00-56_4f328c47` agrees:

| Check | Recorded result |
|---|---|
| Real microphone capture | 64,000 samples / 128,000 bytes / four seconds, shift 15 |
| Input levels | RMS 1,119; peak 9,035; zero clipped samples |
| Upload | 100 frames, all 128,000 bytes, EOF sent and input cleared |
| Session | HTTP 200; WebSocket 101; setup, turn and normal close verified |
| Speaker | 182,400 reply bytes / exactly 91,200 samples played / 3.800 seconds |
| Playback and settling | 3.965 seconds; zero clipped output samples |
| Complete session job | 17.106 seconds, including network work and playback |
| Physical result | Correct spoken answer confirmed; READY recovery passed the client gate |
| Files | Numeric measurements and confirmation only; no recording or transcript saved |

Source JSON SHA256: `c8c16a37c32a05048280d953dfdcf6b0e3c26e19a8ba5876583f12fc248cb0c5`. The source remains in ignored `logs/robot-question-tests/`; the reviewed checkpoint is `logs/esp32-question-live-verified.json`. This is one verified manual turn. Wake integration, repeated wake cycles and follow-up conversation remain the next gates.

## Failure investigation and prerecorded regression

Two initial live attempts returned HTTP 200 but failed before WebSocket upgrade. A session-only reproduction failed too, while the hosted response shape was valid. Device-clock diagnostics then measured the ESP32 **216.6 seconds ahead** of the PC; the old plausibility check skipped synchronization and rejected a fresh token's start window. Fresh synchronization restored session validation, Gemini setup and a fixed greeting.

The next live attempt reached upgrade 101 but failed on an unexpected server message. A synthetic desktop probe identified two `voiceActivity` notices; a complete ESP32 prerecorded test also exposed `inputTranscription`. Both are now handled as metadata, with transcript text discarded. These audio-input messages were not exercised by the earlier fixed-text greeting test.

The successful prerecorded ESP32 run sent **128,000 bytes / 100 frames + EOF**, received **194,400 bytes / 97,200 samples**, played **4.050 seconds** in **4.220 seconds** including settling, closed normally and returned to READY. Source output had zero clipped samples. This proves the audio protocol/playback path with synthetic input, not live microphone intelligibility or a human-confirmed answer.

Developer-only repeat procedure, using a verified local System.Speech fixture:

```powershell
python .\tools\prepare_question_fixture.py
.\tools\idf.ps1 -Action build -Profile question -FixtureCheck
.\tools\idf.ps1 -Action flash -Profile question -Port COM26 -FixtureCheck
```

That temporary diagnostic build adds explicit serial **f**, using synthetic PCM instead of the microphone through the same question transport and speaker path. It is not accepted by the desktop app. Normal build/flash commands explicitly disable the fixture option; the installed image excludes the f handler and embedded fixture. The passing diagnostic image is archived separately. The successful live retest above used normal firmware and the robot's microphone.
