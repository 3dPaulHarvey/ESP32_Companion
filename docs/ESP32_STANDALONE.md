# Standalone wake operation

## Streaming upgrade, 2026-11-19

Version 0.3.0 replaces the normal autonomous question path with live microphone and speaker streaming. The current upload and validation result is recorded in [Response performance](PERFORMANCE.md). The older checkpoints and fixed-capture test procedure below remain historical/diagnostic evidence.

**Uploaded to COM26:** normal v0.3.0, 1,954,224 bytes, SHA256 `4b4910416677b6aa3aed9909c202992150db24220ad2778a70dc453853989a2d`. Flash hashes, preflash checks and all eight normal-image startup/control checks passed; NVS was preserved. The synthetic fixture image was replaced by the normal wake-enabled image.

**Human voice confirmation:** the user heard correct four/twelve replies and reports that continued conversation is fast and connections work reliably. The observer logged two complete variable-length voice turns with zero clipping/underruns, then a third-turn write timeout and a later unclassified observer access interruption. The third capture was intentional continued talking, not a quiet-input false positive. This functional confirmation is not an uninterrupted reliability pass; the incident and timing remain in `logs/live-upgrade-human-review.json` and `logs/live-upgrade-room-test.json`.

Normal operation:

1. At READY, say **Hi, E-S-P** and wait for **ASK NOW**. The microphone warms up for 200 ms while authenticated connection setup starts in parallel.
2. Ask naturally. Local VAD accepts sustained speech, retains 320 ms of leading audio, and streams the question. Recording ends after approximately 600 ms without detected speech. Four seconds is now a wait for speech to begin; the maximum total capture is ten seconds.
3. The screen changes to **CONNECTING** without an end cue or 800 ms cue wait. Incoming reply audio starts playing after a 120 ms buffer, before the complete answer has arrived when possible.
4. After the answer, ASK NOW opens for an optional follow-up. Quiet input is discarded locally, with zero question audio uploaded.
5. READY after quiet retains the connection for the remainder of a 30-second idle window measured from the preceding answer. A new wake during that window reuses the connection and conversation context. The connection closes after idle, eight answers, or two minutes total. A later wake creates a fresh session.

Wi-Fi modem power saving is disabled when the saved station connects. Cloudflare, the Gemini model, the deployed token policy and the calibrated microphone/wake gains are unchanged. There is no barge-in or acoustic echo cancellation: microphone recording finishes before speaker playback starts. Disconnects and ambiguous uploads never trigger automatic question replay.

Use the read-only live observer for this path:

```powershell
python tools/robot_live_observer.py --seconds 180 --report "$env:LOCALAPPDATA/ESP32_Companion/qa/live-voice-trial.json"
```

Choose a new report filename per trial. This tool verifies the device identity and sends only status queries. It records numeric events, not room audio, transcripts or raw serial output. Active reports should use LocalAppData: one atomic report replacement in Dropbox-backed `logs/` returned a permission error during this upgrade. The observer now retries temporary replacement locks; the sync process responsible was not confirmed.

The calibrator and explicit old diagnostic controls retain their fixed four-second captures. The older standalone three-trial app checks a fixed two-answer protocol and does not validate the new variable-length live path. A diagnostic pause while the live connection is warm closes it before transferring control.

**Historical clean-build upload on 2026-11-19:** COM26 previously booted `esp32_companion` version 0.2.0 from the 1,935,552-byte renamed clean build. Its running ELF identity matched. All eight normal-image device/control checks passed, including saved Wi-Fi, local microphone capture with zero clipping, diagnostic handoff and wake re-arming. [Earlier upload evidence](TEST_LOG.md).

**Phase 5c first gate passed on 2026-11-18:** autonomous voice conversations and separate-USB-supply cold boot/re-wake are verified. The `standalone` profile connects saved Wi-Fi at boot. When READY, **Hi, E-S-P** starts the bounded two-answer conversation without a Desktop arm, open serial port or PC heartbeat. A subsequent [upload recovery update](UPLOAD_RECOVERY.md) adds bounded partial/temporary-write handling and passes ten fault-injected synthetic conversations. The original saved run contains one upload failure followed by fresh-wake recovery; its exact native cause was not recorded. The preceding [armed follow-up checkpoint](ESP32_FOLLOWUP.md) remains verified separately. Actual-voice stress, prolonged idle, access-point outage, longer conversations and interruption remain later gates.

## Historical fixed-capture behavior

1. Power the robot. Animated eyes continue while **WIFI...** changes to **READY** after saved Wi-Fi connects. Startup and reconnection alone send no Gemini request.
2. Say **Hi, E-S-P** from 1-2 feet away. Wait for the listening eyes and **ASK NOW** before asking a short question. ASK NOW begins after the microphone warmup; the recording window is four seconds and ends with the local cue.
3. The robot shows CONNECTING, then speaks its answer with the speaking expression.
4. After the reply, another **ASK NOW** window opens automatically. Ask the follow-up without another wake word. The same Gemini connection retains context.
5. After the second answer, stay quiet through the final four-second window. Quiet is discarded locally, the connection closes, and READY returns. A new wake starts a fresh conversation.

This build permits **two answers per conversation**. Silence after the first answer closes it early. It has no barge-in: wait until the robot finishes speaking and shows ASK NOW. Noise can be classified as speech; keep background audio paused for the quiet checks. Capture gain remains shift 15, wake gain 12, and the previously verified VAD gate is unchanged.

If Wi-Fi is unavailable, normal local wake/capture/cue behavior remains, followed by WIFI...; it gives no cloud answer. **LOCAL** indicates an explicit diagnostic Wi-Fi stop. **WAIT** indicates the failure cooldown or server Retry-After window; let it become READY and use a fresh wake. Failed autonomous captures/conversations impose a 30-second cooldown. The firmware does not retry a question automatically. **SETUP** indicates a missing saved device credential.

## Self-paced test

Double-click **Open Robot Standalone Test.cmd** in the project folder. Close other robot apps/serial monitors. Click **Connect robot** and wait for Ready when you are. This app only queries status: it cannot arm, pause, cancel, send a heartbeat or change Wi-Fi. Connection does not probe Gemini.

Run one quiet trial followed by two full trials:

1. Select **First answer, then stay quiet**, click **Record next wake**, and say Hi, E-S-P. At green or ASK NOW ask **What is two plus two?** Expect four; stay quiet through the next window until READY. Confirm the correct clear answer and quiet idle.
2. The two-question option now unlocks. Click Record next wake and repeat the first question. At the next green/ASK NOW ask **And what is that number times three?** Expect twelve. Stay quiet until READY and confirm.
3. Repeat the full two-question trial with a fresh wake, then confirm.

**Record next wake starts logging, not robot control.** The robot also responds while this app is idle or closed. **Stop observing** stops collecting that trial; the robot finishes its own bounded conversation. Reconnect or record another trial once it is READY.

After all three confirmed passes, **Unplugged power check** unlocks. Click it: the observer closes COM26 and presents an autosaved checklist. Unplug the PC USB cable and cold-boot the robot from a separate USB power supply. Use the robot's screen for timing. Ask the same two questions, stay quiet until READY, then try a fresh wake/question once more. Check only the items you actually observed, click **Save passed power check** (or save a problem), then **Finish & close**. This final check is a human report because the PC is disconnected.

Each available capture, reply, speech decision, error, partial trial and human confirmation autosaves under `logs/robot-standalone-tests/<session id>/session.json`. The unplugged checklist saves with each click. A later idle USB error is saved as a separate connection event and preserves completed/confirmed trials; an error during an active trial still marks that trial failed. No microphone recording, transcript, temporary URL or credential is saved. Demo data stays in `robot-standalone-tests-qa/` and does not count as a physical pass. If a save fails, use Retry save before closing.

## Diagnostic compatibility and ownership

The older calibrator/manual/wake/follow-up apps remain usable. On standalone firmware, they first acquire **p**, a ten-second pause of automatic cloud wake. Their existing **=** heartbeat renews that pause. **y** resumes autonomy on close; expiry also resumes it after a lost app/USB connection. A late heartbeat cannot revive an expired pause. Diagnostic w/o commands during the pause are temporary: closing restores the robot's prior desired Wi-Fi state. An explicit o outside a pause keeps Wi-Fi off until w or reset.

Explicitly armed Desktop conversations still require their owner's ten-second heartbeat. Autonomous conversations have a different owner flag and require no PC heartbeat; transport/playback/quiet closure and all deadlines run on the ESP32. Both paths retain the two-answer limit and the two-minute network deadline. Serial logging uses the installed SDK's disconnected-USB behavior; no vendor driver or logger source was changed.

The RAM-only `LAST_CONVERSATION` status holds numeric last-job totals so a developer can check completion after reconnecting USB. It contains no PCM or recognized text and disappears on reset. The observer verifies a physical model wake, autonomous owner, complete captures, one connection, exact uploads/playback, quiet discard, normal close and READY.

## Build and developer checks

```powershell
$taskBuildRoot = Join-Path $env:LOCALAPPDATA 'ESP32_Companion'
.\tools\idf.ps1 -Action build -Profile standalone -BuildRoot $taskBuildRoot
python .\tools\check_standalone_build.py
.\tools\idf.ps1 -Action flash -Profile standalone -BuildRoot $taskBuildRoot -Port COM26
python .\tools\check_standalone_device.py
python -m unittest discover -s tools -p 'test_*.py'
```

Source, configuration defaults, tools and recovery files remain inside the project. Only generated build output uses `%LOCALAPPDATA%\ESP32_Companion\build-standalone`, outside Dropbox to avoid transient Windows sharing locks. Use the same BuildRoot for build and flash. The wrapper verifies MAC `44:bd:8d:ec:38:e0` on COM26 and preserves NVS. No source relocation or symlink is required for standalone runtime; the installed firmware uses saved Wi-Fi/NVS and the Worker.

Developer-only synthetic verification adds `-FixtureCheck` to build/flash, then runs `check_standalone_build.py --fixture` and `check_standalone_device.py --fixture`. Prepare public PCM with `prepare_followup_fixture.py` first if needed. That image **disables automatic cloud wake** and accepts explicit f for two synthetic inputs followed by zero-filled quiet. It opens no microphone for those inputs. The check closes serial for 45 seconds after the first input, then reads the numeric last-job result and idle status. Restore the normal image afterward. The user observer excludes fixtures.

## Evidence and remaining reliability gates

- 139 compiled policy assertions, including twelve standalone pause/offline/cooldown/admission checks.
- All 80 host/app regression tests passed, including read-only commands, physical-wake ownership, quiet gating, lease-loss cleanup, a partial-old-status USB reopen regression and the actual Tk three-trial/disconnect/autosave workflow.
- The synthetic ESP32 exchange passed with serial closed for 45 seconds: 256,000 input bytes, two complete answers, 97,440 played samples, one quiet discard and acknowledged close 1000. No PC heartbeat or live microphone upload; READY/wake rearmed on reconnection.
- All eight normal-image startup, diagnostic handoff, network and observer checks pass in `logs/esp32-standalone-hardware-qa.json`. They upload no microphone audio and issue no provider request. An old partial USB status response initially made the observer reject the correct profile; anchoring identity to a complete capture-policy header fixes it, with a host regression and successful hardware rerun.
- The saved real quiet trial and two subsequent full trials pass exact counts, autonomous model-wake ownership, same-connection context, quiet closure and READY, with correct clear answers confirmed by the user. The separate USB-supply checklist confirms cold boot without PC USB, two answers, quiet READY and another fresh wake. That unplugged portion is human evidence; numerical USB data covers the preceding observed trials.

The human-verified standalone checkpoint app is **2,596,032 bytes**, SHA256 `90c0277461594eef9d143c044725a5cb0b8a0ad9e7b4958d5a4f700f389a81bd`, archived under `recovery/2026-11-18-phase-5c/built-standalone/`. The current upload-recovery app is **2,599,264 bytes**, SHA256 `091c6f19f39c2faa4cabaf60292f5566a056036c4ede4d099de84be752824b33`, archived under `recovery/2026-11-18-upload-reliability/built-upload-final/`. It compiles 155 policy assertions; all 84 host/app tests pass. Normal preflash checks exclude both synthetic PCM fixtures and fault-injection code, preserve strict TLS CA/name/date verification, compile private logging out of all five transport units, exclude the actual device credential, and preserve NVS. [Upload recovery evidence](UPLOAD_RECOVERY.md).

The private pre-standalone full flash is `recovery/2026-11-18-phase-5c/before-standalone-full-flash.bin`, 8,388,608 bytes, SHA256 `7f78a1f2632da24f51c66aac466cb3c47fc65fb6604d0e81d1cc971c29507e42`; its application matches the verified Phase 5b image byte-for-byte. Earlier backups remain intact. The passing diagnostic app is archived separately (`built-standalone-fixture/`, SHA256 `079ec1a96a00a615ff87ca2a2ef7cb77ce298e44e03bda52d7e3077dd9166f8b`). Recovery files remain private because full flash includes saved credentials. [Recovery procedure](BUILD_AND_FLASH.md).

## Reviewed real session

Closed, non-demo session **2026-11-18_17-46-59_cc36a560** contains six attempts, including additional repeats and an upload failure. The original JSON is unchanged, SHA256 `fab8d45e76d2c0e5e94bc67f510100264093855576f78b498943ddb847f77340`; the derived review is `logs/esp32-standalone-live-verified.json`.

| Gate trial / saved attempt | Wake / baseline | Capture IDs | Answers / playback duration | Follow-up VAD speech / longest | Final quiet VAD | Network job |
|---|---|---|---|---|---|---|
| Quiet / 1 | 1 / 0 | 2, 3 | 1 / 4.240 s | No second upload | 4 / 4, rejected | 23.993 s |
| Full / 4 | 4 / 3 | 8, 9, 10 | 2 / 2.170 s and 2.280 s | 65 / 16, accepted | 0 / 0, rejected | 36.105 s |
| Full / 5 | 5 / 4 | 11, 12, 13 | 2 / 4.240 s and 2.600 s | 77 / 57, accepted | 4 / 4, rejected | 40.186 s |

These three confirmed trials include **eight exact captures**, **five accepted uploads / 640,000 bytes**, **372,723 played samples**, and **three quiet segments / 384,000 bytes discarded locally**. Both full trials occur after the failed attempt and demonstrate recovery and a fresh wake after closure. The observer sends zero arms, heartbeats or network commands. There is no gain/VAD change.

All five protocol-complete, human-confirmed attempts (1, 2, 4, 5 and 6) total nine spoken answers / 740,886 played samples, 1,152,000 accepted input bytes and five locally discarded quiet segments. All fifteen measured captures, including the failed attempt's input, have zero clipping and no raw near-full-scale words. All nine completed outputs have zero clipping.

**Historical reliability incident:** attempt 3 fails during input upload with HTTP 200, WebSocket upgrade 101 and `audio_send_failed`, before any completed reply. The number of uploaded bytes at failure and the lower-level transport cause were not saved, so that historical cause is undetermined. Later fresh wakes succeed. The subsequent [upload recovery change](UPLOAD_RECOVERY.md) fixes one-shot short/temporary-write handling and preserves future failure diagnostics, with ten controlled synthetic sessions passing. This original real session is not a clean uninterrupted reliability run; actual-voice stress, longer idle and broader outage checks remain open.

Attempt 6 completed its protocol result and received human confirmation before a later COM26 access error. The original app overwrote its state with `error`; the underlying completion, metrics and earlier human confirmation remain. This trial is recorded separately from the clean gate trials. The Desktop app now preserves completed/review-pending/confirmed trials and saves subsequent idle connection errors separately. Regression tests also verify that active-trial failures still fail. The source session was not rewritten. Firmware was unchanged during that review; the later upload-recovery update is recorded separately above.

The unplugged checklist has all three checks true, `observer_disconnected=true`, `human_only=true` and `human_confirmed=true`; the user also reports the power check passed. The app closed at **17:56:33 PST on November 18** / 01:56:33 UTC on November 19. The standalone functional gate is verified; broader reliability remains open.
