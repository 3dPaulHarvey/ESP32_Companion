# First manual microphone question

2026-11-18: COM26 runs the **question** profile with reviewed microphone settings. Build, framing, host-app and local cancellation/recovery checks pass. **The actual spoken Gemini answer still needs the user's test.** Wake-triggered cloud conversation is a later step.

## Run when back at the robot

1. Connect the development robot on COM26 and close serial monitors. Open **Robot Spoken Question Test** on the Desktop, or **Open Robot Question Test.cmd** in the project.
2. Click **Connect robot**. The app verifies the development MAC, checks capture shift 15, connects the robot's saved Wi-Fi, and pauses incidental wake recognition. It opens no Gemini session yet.
3. From 1–2 feet away in a quiet room, click **Start question**. Wait for the countdown and green **SPEAK NOW**, then say **“What is two plus two?”** once at normal volume.
4. Stop at the local end cue. The robot shows CONNECTING, receives a reply, shows SPEAKING while playing it, then returns to READY.
5. Choose **Yes — clear answer: four** only if you heard a correct answer at normal speed and saw READY. Otherwise select **Answer or audio problem**. Click **Finish & close** and tell Codex the test is finished.

Each Start authorizes exactly one four-second microphone capture to be sent to Gemini. It sends the captured PCM, without a text version of the question. No audio recording or transcript is saved on the PC. Connection alone, normal wake, and the microphone calibrator never start cloud questions. Closing normally cancels pending work, restores wake, releases COM26 and turns the robot's Wi-Fi off. A reply already playing may finish while cleanup waits; it is bounded to ten seconds. A disconnected USB cable may prevent host cleanup; resetting the robot returns to its Wi-Fi-off startup.

Every attempt and its available numeric microphone/network/playback result is autosaved under `logs/robot-question-tests/<session id>/session.json`; `latest-session.json` points to the newest run. Microphone measurements are saved as soon as capture completes, including when a later cloud step fails. The user's physical confirmation is separate from transport success. Partial/cancelled runs remain available. If saving fails, the app preserves the prior JSON and offers Retry save. Demo/test results live in separate QA folders and cannot establish a real pass.

## Applied microphone measurements

The accepted 2026-11-18 calibration contains three quiet and three normal-speech captures. All six have exact four-second counts; all three speech samples were confirmed to fit the window. At capture shift 14, speech had **zero clipped samples**, a maximum peak of **17,049 / 32,768**, and a median active-speech/quiet **level contrast of 12.72 dB**. The same raw input would have hit the old shift-12 clipping limit 2,067 times. No near-full-scale raw words were reported. The optional wake test was skipped; earlier physical wake verification remains valid.

Capture shift **15** is now configured in both microphone and question profiles to preserve the chosen minimum 6 dB peak-headroom target across all accepted raw peaks. The predicted maximum at that gain is about **8,525 / 32,768** (about 11.7 dB below full scale); this is a numerical prediction, not a second human recording. Wake recognition retains shift **12**. Duration stays **four seconds**, with no new noise gate or filter. The upcoming spoken-question result will establish intelligibility at the applied gain.

Original session: ignored `logs/microphone-calibration/2026-11-18_12-20-12_18333abe/session.json`, SHA256 `488860de012e4eedd394ceaa223824c2cc23f58bc6368ccb1be87694c404636f`. Its measured policy remains unchanged at 14; the applied decision is recorded separately in `logs/microphone-calibration-applied.json`.

## Firmware ownership and limits

Only explicit serial **q** can retain a question capture. The existing input task remains the sole microphone reader. It transfers the successful 128,000-byte allocation directly to the session task; the UI receives only metadata. Normal wake/calibration PCM is still wiped before metadata publication. Failed, cancelled, stale, rejected and pre-upload network-loss paths clear retained input.

After the end cue, the session task obtains a fresh temporary token through the existing Worker, opens the verified constrained Gemini WebSocket, and sends **100 PCM frames of 640 samples / 40 ms**, followed by `audioStreamEnd`. Incoming messages and ping/close controls are processed during upload. Input and base64 workspaces are wiped; the input allocation is released immediately after EOF, or on any earlier failure. This uses the existing server-side activity detection and EOF signal. [Google's realtime input fields](https://googleapis.github.io/js-genai/release_docs/interfaces/types.LiveClientRealtimeInput.html).

The established reply validator still enforces 24 kHz mono PCM, complete nonempty output, normal closure, a ten-second/480,000-byte audio bound, and exact playback counts. The cloud connection closes before playback. The output task remains the sole speaker writer, and wake waits for settling. Setup, upload, reply, closure and playback all have finite deadlines. A failed request never retries automatically. Worker code, key storage, dependency versions and TLS checks are unchanged.

```powershell
.\tools\idf.ps1 -Action build -Profile question
.\tools\idf.ps1 -Action flash -Profile question -Port COM26
python .\tools\test_robot_question.py
```

The flash wrapper verifies the development MAC. `question` uses separate `build-question/` and `sdkconfig.question.generated`; `microphone` remains the local-only alternative. Both now seed shift 15. Existing generated configurations are not overwritten by default files; the local microphone configuration was explicitly updated before its regression build.

Unattended hardware checks verified offline/held-control rejection, early cancellation, busy guards, retained-input discard when Wi-Fi disappears before upload, subsequent ordinary capture, and idle/wake recovery. The real question-app adapter also connected and closed successfully. These checks made **no provider request and uploaded no room audio**. They establish readiness to perform the user's spoken question, not a completed conversation milestone. [Roadmap](ROADMAP_STATUS.md).
