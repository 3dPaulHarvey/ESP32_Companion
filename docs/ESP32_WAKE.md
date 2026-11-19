# One wake, one spoken question

**Phase 5a passed on 2026-11-18:** three consecutive physical Hi, E-S-P wakes each received a correct clear spoken answer and returned to READY. Saved technical results and the user's confirmations agree. This is a controlled single-turn checkpoint: **one click arms one recognized wake, which permits one question and one reply**. Follow-up listening is the next step.

## Run at your own pace

1. Connect the development robot on **COM26** and close serial monitors. Open **Robot Wake Question Test** on the Desktop, or **Open Robot Wake Test.cmd** in the project.
2. Click **Connect robot** and wait for **Ready when you are**. The app verifies the board, calibrated capture policy, Wi-Fi and a Gemini setup/close check. Connection alone starts no question.
3. In a quiet room, sit 1-2 feet from the robot. Click **Arm next wake**, then say **Hi, E-S-P**, speaking the three letters separately. You have one minute to wake it.
4. Wait for green **SPEAK NOW**, then say **What is two plus two?** once at normal volume. The robot holds LISTENING during the four-second capture. Stop speaking at the end cue.
5. Expect CONNECTING, a spoken Gemini answer with the SPEAKING face, and then READY. Select **Yes - clear answer: four** only if the answer was correct, speech sounded normal, and READY returned. Otherwise select **Answer or audio problem**.
6. Click **Arm next wake** again for another independent trial. Try **three separate cycles**, confirming each result. Click **Finish & close** and tell Codex when finished. There is no need to coordinate a speaking window in chat.

Each arm is consumed by the first recognized wake. The robot does not listen for a follow-up cloud question after its answer. Between trials the app pauses incidental wake detection. Closing normally cancels pending work, turns Wi-Fi off, restores local wake and releases COM26. **Stop** cancels an arm or question; a reply already playing may finish during bounded cleanup.

An arm expires after one minute, even if the app keeps running. If the app disconnects or stops sending heartbeats while waiting, authorization expires within ten seconds. Wi-Fi loss and calibration takeover also disarm it. None of these events automatically retries a question. Reset returns to Wi-Fi-off, unarmed startup. Outside an armed trial, normal wake stays on the local capture/cue path.

## Saved evidence

Every real session autosaves to `logs/robot-wake-tests/<session id>/session.json`; `latest-session.json` identifies the newest run. Each attempt preserves partial/error/cancelled status, available numeric microphone/network/playback results, and the user's separate audible-result confirmation. Microphone measurements save before the cloud reply completes. No microphone recording, transcript, temporary credential or raw provider message is saved.

The client accepts a turn only when a new physical wake count exceeds its arm baseline, the model-triggered cycle matches the microphone capture, exactly one question/session opens, all 128,000 input bytes and EOF are sent, reply validation and exact speaker playback pass, and READY returns. Its USB allowlist excludes manual `q`, synthetic `f`, local `n` and provisioning commands. This prevents the tester from substituting a serial-started question for the wake checkpoint.

Demo runs use `--demo`, save separately under `logs/robot-wake-tests-qa/`, and cannot establish a physical pass. The [manual spoken question tester](ESP32_QUESTIONS.md) remains available as a reference; the [microphone calibrator](MICROPHONE_CALIBRATOR.md) remains local-only.

## Firmware and recovery

The opt-in **wake** profile adds `CONFIG_COMPANION_CLOUD_WAKE=y` to the question configuration. Boot is unarmed. Serial `a` atomically hands an idle calibration hold to one wake permission; `z` disarms a waiting permission; `x` also invokes the existing question cancellation. `=` renews the waiting owner's ten-second lease but cannot extend its one-minute window or revive an expired arm. Extra session requests are blocked while that permission owns the next question.

The recognized wake uses the existing controller with source `voice_question`. It consumes the arm before capture starts and reuses the same microphone allocation ownership, calibrated shift **15**, **four-second** input, end cue, temporary-token request, bounded Gemini reply, speaker task and settling. Wake gain remains **12**. During capture, cloud processing and playback, wake recognition is paused. Worker code, API-key storage, TLS validation, provider model and dependency versions are unchanged.

From the repository root:

```powershell
$taskBuildRoot = Join-Path $env:LOCALAPPDATA 'ESP32_Companion'
.\tools\idf.ps1 -Action build -Profile wake -BuildRoot $taskBuildRoot
.\tools\idf.ps1 -Action flash -Profile wake -BuildRoot $taskBuildRoot -Port COM26
python -m unittest discover -s tools -p 'test_*.py'
```

`-BuildRoot` puts generated artifacts in `<root>/build-wake/`, outside the synced workspace; source and `sdkconfig.wake.generated` stay in the project. Use the same root for build and flash. Without that option, the usual project-local `build-wake/` is used. The wrapper verifies the development MAC before flashing, preserves NVS, and disables the synthetic fixture. `-FixtureCheck` is available only in the separate question and followup diagnostic profiles.

The prior verified manual image and original full-flash backups are retained. A new private pre-wake full-flash backup is `recovery/2026-11-18-phase-5a/before-wake-full-flash.bin`, 8,388,608 bytes, SHA256 `751c0b8e0de35279416d8c2043e3e397ef7f6446da5c781e99707725ef5a0373`. Use it only for the same development board. [Build and recovery procedure](BUILD_AND_FLASH.md).

## Installed checkpoint and verification

Installed on COM26 on 2026-11-18: **2,583,152 bytes**, SHA256 `239c8f5189878702d850c8ab57be1471ba423859a29de67f44983e5eb0939ee0`. Guarded flash verified the development identity and write hashes. The exact write files/configuration/manifest are archived privately under `recovery/2026-11-18-phase-5a/built-wake/`. The pre-wake backup was also checked to contain the preceding verified manual app byte-for-byte.

| Verification | Result |
|---|---|
| Firmware policies | 116 compiled assertions, including six new one-use/lease/window checks |
| Host and actual Tk workflows | 62 tests pass; includes manual tester and full calibrator regression |
| Board boot and app connection | Offline/unarmed boot; calibrated policy and Gemini setup/normal-close pass before arming |
| Board ownership | Duplicate arm and competing session rejected; explicit disarm and calibration takeover recover |
| Board deadlines | Ten-second missing-heartbeat expiry; late heartbeat does not rearm; active heartbeats cannot extend the one-minute window |
| Board Wi-Fi recovery | Wi-Fi stop disarms; offline arm rejected; reconnect does not restore permission |
| Board local capture recovery | Exact four-second/64,000-sample capture and cue at shift 15, zero clipping |
| Unattended input policy | Zero question requests; one setup-only provider check; no microphone audio uploaded |
| Physical wake/question/answer | PASS: three consecutive correlated wakes, complete turns, READY and correct clear answers confirmed by the user |

Ignored numeric evidence: `logs/esp32-wake-preflash.json`, `logs/esp32-wake-hardware-qa.json`; full build/flash/host logs are stored beside them. Automated checks cover policy deadlines, one-use permission, physical-event correlation in controlled fixtures, cancellation, complete-turn gates and actual Tk save/review/close flows. The real trials below separately establish audible speech and correct answers.

## Verified real wake trials

Closed, non-demo session **2026-11-18_15-40-39_be9779c7** contains exactly three attempts. Each is `verified_by_user` with `human_confirmed=true`; the user also reports three successful cycles in chat. Every capture used shift 15 and exactly 64,000 samples, sent 128,000 bytes / 100 frames plus EOF, cleared input, verified HTTP 200 / upgrade 101 / setup / completed turn / normal close 1000, played every reply sample and passed the client's READY gate. Input and output clipping were zero in all three.

| Trial | Physical wake count / arm baseline | Capture cycle | Input RMS / peak | Reply samples / duration | Session job, including playback |
|---|---|---|---|---|---|
| 1 | 2 / 1 | 3 | 973 / 6,006 | 103,440 / 4.310 s | 18.377 s |
| 2 | 3 / 2 | 4 | 924 / 5,672 | 53,040 / 2.210 s | 14.305 s |
| 3 | 4 / 3 | 5 | 844 / 6,003 | 91,921 / 3.830 s | 17.256 s |

Source: ignored `logs/robot-wake-tests/2026-11-18_15-40-39_be9779c7/session.json`, SHA256 `da4183efbf230c4360ad9b64b908d403cdd47d66bc53211ec7541c8034232b36`; reviewed summary: `logs/esp32-wake-live-verified.json`. Closed at 23:42:16 UTC / 15:42:16 PST. No recording or transcript was saved. This establishes three separate armed single turns; follow-up listening, quiet timeout and a later fresh wake remain the next conversation checkpoint.
