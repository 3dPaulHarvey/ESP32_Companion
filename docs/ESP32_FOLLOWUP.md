# Bounded follow-up conversation

**Phase 5b passed on 2026-11-18:** a quiet-control trial and two complete real follow-up conversations succeeded, including quiet closure and a fresh physical wake. The user confirms five correct clear answers; all eight captures and five playbacks have zero clipping. The verified sequence is **one armed wake, two questions on the same Gemini connection, then quiet timeout and READY**. The earlier [single-turn checkpoint](ESP32_WAKE.md) remains available.

## Run the self-paced test

Double-click **Open Robot Conversation Test.cmd** in the project folder. Close other robot apps and serial monitors, connect COM26, and click **Connect robot**. Wait for **Ready when you are**. Connection alone starts no question.

Run these three trials in a quiet room, 1-2 feet from the robot:

The two-question option unlocks after the quiet check completes and you confirm its result.

1. **First answer, then stay quiet.** Select the first option, click **Arm conversation**, and say **Hi, E-S-P**. At green **SPEAK NOW**, ask **What is two plus two?** Stop at the cue. After the robot answers four, stay quiet through **STAY QUIET**. It should close the conversation and return to READY. Confirm only if speech was clear, the answer was correct and quiet idle worked.
2. **Two questions, then stay quiet.** Select the second option and arm a fresh wake. Ask the same first question at green. After the answer, a second green window appears automatically: ask **And what is that number times three?** without another wake word or click. Expect twelve. Stay quiet after that answer until READY, then confirm the full result.
3. **Repeat the two-question trial.** Arm another fresh wake and repeat both questions. This checks a new conversation after the prior session closed.

Click **Finish & close** and tell Codex when done. Each trial starts with your click; there is no need to coordinate speaking windows in chat. Both speaking windows are four seconds, with an end cue. The app previews the follow-up phrase while the first answer plays. Follow **SPEAK NOW** for questions and **STAY QUIET** for the timeout check.

This checkpoint permits two spoken answers per arm. After each answer it checks one four-second local microphone segment. A segment classified as quiet is wiped and closes Gemini without uploading that segment. After the second answer, further speech ends at the two-turn limit. The test expects actual silence at the end and records a problem if speech is detected there. No question is retried automatically.

## Saved results and controls

Real sessions autosave under `logs/robot-conversation-tests/<session id>/session.json`; `latest-session.json` points to the newest run. Available capture metrics, each completed reply's playback metrics and local speech decisions save before the final conversation result. Errors, cancellations and partial trials remain available. Human confirmation is separate from the protocol result.

No microphone recording, transcript, raw server message or credential is saved. The first input still follows an explicitly armed physical wake; the client excludes serial/manual questions, synthetic fixtures and provisioning commands. It verifies the new wake count/capture cycle, one Worker request, one Gemini connection, exact input counts for each accepted turn, complete output/playback, normal close, the expected speech/silence decisions and READY.

**Stop** cancels waiting or active work. **Finish & close** saves, cancels pending work, closes Gemini, turns Wi-Fi off and restores local wake. A speaker reply already playing may finish during bounded cleanup. Waiting arms retain the established ten-second heartbeat lease and one-minute maximum. During a conversation's network job, losing its owner's heartbeats triggers cancellation after ten seconds; late heartbeats cannot revive a cancelled job. Reset returns to offline/unarmed startup.

Demo results (`--demo`) are isolated under `logs/robot-conversation-tests-qa/` and cannot establish a physical pass. If saving fails, keep the window open and use **Retry save**; earlier files are preserved.

## Implementation and limits

The opt-in **followup** profile adds `CONFIG_COMPANION_FOLLOWUP=y` above the wake profile. Explicit serial **c** arms a bounded conversation; **a** retains the earlier one-question wake behavior, and **q** remains the manual reference. Boot stays unarmed with Wi-Fi off. The PC owns the test controls, but microphone capture, speech classification, transport, playback and cleanup run on the ESP32.

The microphone input task remains the sole I2S reader. Follow-up requests pass through the same local controller and four-second capture allocation, with source `followup_question`; only the session task takes retained PCM. The capture stays at shift **15**, wake at **12**, with the existing warmup and output settling. Cancellation/network exits clear producer-owned completed segments as well as session-owned input before restoring wake.

Local speech classification uses the installed ESP-SR `esp_vad` API at mode **3**, over exactly **200 frames of 20 ms / 320 samples**. The gate requires at least ten positive frames and a run of at least five. Numeric counts are saved for later adjustment. Classification happens on the complete bounded capture before its upload. It does not add a filter or change microphone gain.

The new bounded client keeps a single verified WebSocket open across both replies. One network owner pumps ping/close/metadata while a helper queues playback through the existing output task. The reply accumulator is wiped/reset between turns. After each accepted 128,000-byte input, it sends 100 frames and `audioStreamEnd`; the next accepted input uses the same connection and context. Google's documentation describes a session as one persistent connection and permits resuming audio after `audioStreamEnd`. [Session management](https://aistudio.google.com/docs/live-api/session-management). [Automatic VAD and stream end](https://aistudio.google.com/docs/live-api/capabilities#use-automatic-vad).

Each reply remains bounded to ten seconds/480,000 bytes with strict JSON/audio/completion checks and exact speaker sample accounting. The connection has a two-minute local deadline, finite capture/setup/upload/reply/close deadlines, two-answer maximum and no automatic retry. Tools, resumption, context compression, speech interruption and longer sessions are later work. The Worker, API-key storage, model, TLS verification, GPIOs and dependency versions are unchanged.

## Build and repeatable synthetic checks

```powershell
$taskBuildRoot = Join-Path $env:LOCALAPPDATA 'ESP32_Companion'
.\tools\idf.ps1 -Action build -Profile followup -BuildRoot $taskBuildRoot
.\tools\idf.ps1 -Action flash -Profile followup -BuildRoot $taskBuildRoot -Port COM26
python -m unittest discover -s tools -p 'test_*.py'
```

Generated artifacts and configuration use `<root>/build-followup/`, with the generated configuration at `build-followup/sdkconfig`. The default root is `%LOCALAPPDATA%/ESP32_Companion`, outside Dropbox. Use the same `-BuildRoot` for build and flash. The wrapper verifies the development MAC and preserves NVS. Normal commands disable embedded test audio.

Developer-only synthetic regression:

```powershell
python .\tools\prepare_followup_fixture.py
.\.tools\node-v24.22.3-win-x64\node.exe .\tools\check_followup_context.mjs
.\tools\idf.ps1 -Action build -Profile followup -BuildRoot $taskBuildRoot -FixtureCheck
.\tools\idf.ps1 -Action flash -Profile followup -BuildRoot $taskBuildRoot -Port COM26 -FixtureCheck
```

That diagnostic image adds explicit **f**, sending the public synthetic math question and context-dependent follow-up, then checking a zero-filled quiet segment locally. It opens no microphone for those inputs. The desktop context test verifies four then twelve, retaining only answer-presence booleans and numeric counts. Restore the normal build/flash afterward; the user-facing tester never sends f. The preceding question profile's fixture option remains available separately.

The private pre-follow-up full-flash backup is `recovery/2026-11-18-phase-5b/before-followup-full-flash.bin`, 8,388,608 bytes, SHA256 `356dba0a32962e44e135f839273209fb333d51cbe2768e738fc73161452e0a28`. Its OTA0 image matches the verified Phase 5a firmware byte-for-byte. Earlier backups remain intact. [Recovery procedure](BUILD_AND_FLASH.md).

## Verification status

- 127 compiled policy assertions, including eleven follow-up count/speech/budget checks.
- All 70 Python host and actual Tk workflow tests pass, including earlier testers and full calibrator regression.
- Desktop synthetic context test passes four then twelve over one connection, both with complete audio and normal close.
- ESP32 synthetic two-turn exchange passes complete upload, both speaker playbacks, local quiet classification and normal close without microphone input.
- Synthetic active cancellation and missing-heartbeat cancellation both restore READY. In-flight server audio can exceed the bounded close-drain allowance; those cancellation cases release the transport after that limit rather than waiting indefinitely for close acknowledgment.
- Ten normal-image board checks pass: offline/unarmed boot, app preflight, duplicate/competing requests, disarm, lease/window expiry, calibration takeover, Wi-Fi stop/reconnect and local capture recovery. They issued one setup-only provider request and uploaded no microphone audio.
- The quiet-first UI gate and complete save/review/stop/close workflow pass actual Tk event-loop tests. The three real trials below pass separately with human confirmation.

Installed normal application: **2,592,416 bytes**, SHA256 `9854184e25abb717148410cc790c57be37f2d9258819424b628fb616565c432f`. Guarded flashing verifies the development MAC and write hashes. Normal configuration, compile list, image strings and CMake cache exclude the synthetic fixture; CA/name/date checks, private-transport log suppression and NVS preservation pass. Exact write files/configuration/manifest are saved under `recovery/2026-11-18-phase-5b/built-followup/`; passing synthetic images are archived separately.

Ignored evidence: `logs/gemini-followup-context.json`, `logs/esp32-followup-fixture-final-hardware.json`, `logs/esp32-followup-preflash.json`, `logs/esp32-followup-hardware-qa.json` and `logs/robot-conversation-host-tests.log`. Synthetic results establish transport/context/playback, while the user-run microphone tests establish intelligibility and audible correctness.

The broader roadmap still includes standalone wake operation, longer turn-taking, interruption, prolonged idle and repeated network-fault recovery. This checkpoint first verifies the bounded follow-up sequence using saved user-run tests.

## Verified real trials

Closed, non-demo session **2026-11-18_16-34-53_8d1f2d33** contains exactly three attempts: quiet, followup, followup. Every attempt is `verified_by_user` with `human_confirmed=true`; the user also reports that all three succeeded. All captures contain exactly 64,000 samples at shift 15. Each accepted spoken turn sends 128,000 bytes/100 frames plus EOF and clears its input. Both full conversations preserve one connection and pass complete reply playback, normal close, quiet timeout and READY.

| Trial | New wake / baseline | Capture request IDs | Spoken answers / reply duration | Follow-up VAD positives / longest | Final quiet VAD | Session job |
|---|---|---|---|---|---|---|
| Quiet control | 4 / 3 | 8, 9 | 1 / 4.030 s | No follow-up upload | 4 / 4, rejected | 23.515 s |
| Full conversation 1 | 5 / 4 | 10, 11, 12 | 2 / 2.970 s and 4.440 s | 87 / 29, accepted | 4 / 4, rejected | 40.370 s |
| Full conversation 2 | 6 / 5 | 13, 14, 15 | 2 / 2.080 s and 2.450 s | 76 / 53, accepted | 4 / 4, rejected | 34.373 s |

In total, **five real questions uploaded 640,000 bytes**, while **three quiet captures totaling 384,000 bytes were discarded locally**. All 383,284 reply samples were played. Input and output clipping were zero. Quiet-window RMS varied from 346 to 1,159 while both follow-up speech windows were accepted; these trials support keeping the current VAD policy and calibrated gain unchanged.

Source JSON: ignored `logs/robot-conversation-tests/2026-11-18_16-34-53_8d1f2d33/session.json`, SHA256 `776655fa92ba6b31941a99b303f8b955607cbacba0658ee23aa305a509ab686c`. The source is unchanged; reviewed summary is `logs/esp32-followup-live-verified.json`. Closed at 00:37:14 UTC on November 19 / **16:37:14 PST on November 18**. No recording or transcript was saved. Standalone operation without the Desktop arm remains the next checkpoint.
