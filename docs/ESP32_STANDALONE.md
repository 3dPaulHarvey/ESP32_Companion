# Standalone wake operation

**Phase 5c first gate: installed for self-paced physical verification.** The `standalone` profile connects saved Wi-Fi at boot. When READY, **Hi, E-S-P** starts the existing bounded two-answer conversation without a Desktop arm, open serial port or PC heartbeat. The preceding [armed follow-up checkpoint](ESP32_FOLLOWUP.md) remains verified separately. Ten-cycle stress, prolonged idle, access-point outage, longer conversations and interruption remain later gates.

## Expected robot behavior

1. Power the robot. Animated eyes continue while **WIFI...** changes to **READY** after saved Wi-Fi connects. Startup and reconnection alone send no Gemini request.
2. Say **Hi, E-S-P** from 1-2 feet away. Wait for the listening eyes and **ASK NOW** before asking a short question. ASK NOW begins after the microphone warmup; the recording window is four seconds and ends with the local cue.
3. The robot shows CONNECTING, then speaks its answer with the speaking expression.
4. After the reply, another **ASK NOW** window opens automatically. Ask the follow-up without another wake word. The same Gemini connection retains context.
5. After the second answer, stay quiet through the final four-second window. Quiet is discarded locally, the connection closes, and READY returns. A new wake starts a fresh conversation.

This build permits **two answers per conversation**. Silence after the first answer closes it early. It has no barge-in: wait until the robot finishes speaking and shows ASK NOW. Noise can be classified as speech; keep background audio paused for the quiet checks. Capture gain remains shift 15, wake gain 12, and the previously verified VAD gate is unchanged.

If Wi-Fi is unavailable, normal local wake/capture/cue behavior remains, followed by WIFI...; it gives no cloud answer. **LOCAL** indicates an explicit diagnostic Wi-Fi stop. **WAIT** indicates the failure cooldown or server Retry-After window; let it become READY and use a fresh wake. Failed autonomous captures/conversations impose a 30-second cooldown. The firmware does not retry a question automatically. **SETUP** indicates a missing saved device credential.

## Self-paced test

Open **Robot Standalone Test** on the Desktop or **Open Robot Standalone Test.cmd** in the project. Close other robot apps/serial monitors. Click **Connect robot** and wait for Ready when you are. This app only queries status: it cannot arm, pause, cancel, send a heartbeat or change Wi-Fi. Connection does not probe Gemini.

Run one quiet trial followed by two full trials:

1. Select **First answer, then stay quiet**, click **Record next wake**, and say Hi, E-S-P. At green or ASK NOW ask **What is two plus two?** Expect four; stay quiet through the next window until READY. Confirm the correct clear answer and quiet idle.
2. The two-question option now unlocks. Click Record next wake and repeat the first question. At the next green/ASK NOW ask **And what is that number times three?** Expect twelve. Stay quiet until READY and confirm.
3. Repeat the full two-question trial with a fresh wake, then confirm.

**Record next wake starts logging, not robot control.** The robot also responds while this app is idle or closed. **Stop observing** stops collecting that trial; the robot finishes its own bounded conversation. Reconnect or record another trial once it is READY.

After all three confirmed passes, **Unplugged power check** unlocks. Click it: the observer closes COM26 and presents an autosaved checklist. Unplug the PC USB cable and cold-boot the robot from a separate USB power supply. Use the robot's screen for timing. Ask the same two questions, stay quiet until READY, then try a fresh wake/question once more. Check only the items you actually observed, click **Save passed power check** (or save a problem), then **Finish & close**. This final check is a human report because the PC is disconnected.

Each available capture, reply, speech decision, error, partial trial and human confirmation autosaves under `logs/robot-standalone-tests/<session id>/session.json`. The unplugged checklist saves with each click. No microphone recording, transcript, temporary URL or credential is saved. Demo data stays in `robot-standalone-tests-qa/` and does not count as a physical pass. If a save fails, use Retry save before closing.

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

## Evidence and remaining gate

- 139 compiled policy assertions, including twelve standalone pause/offline/cooldown/admission checks.
- All 80 host/app regression tests passed, including read-only commands, physical-wake ownership, quiet gating, lease-loss cleanup, a partial-old-status USB reopen regression and the actual Tk three-trial/disconnect/autosave workflow.
- The synthetic ESP32 exchange passed with serial closed for 45 seconds: 256,000 input bytes, two complete answers, 97,440 played samples, one quiet discard and acknowledged close 1000. No PC heartbeat or live microphone upload; READY/wake rearmed on reconnection.
- All eight normal-image startup, diagnostic handoff, network and observer checks pass in `logs/esp32-standalone-hardware-qa.json`. They upload no microphone audio and issue no provider request. An old partial USB status response initially made the observer reject the correct profile; anchoring identity to a complete capture-policy header fixes it, with a host regression and successful hardware rerun.
- Real quiet/follow-up/re-wake trials and separate USB-supply cold boot remain **pending user verification**. Software tests and the disconnected serial test do not establish the unplugged power pass.

Installed normal app: **2,596,032 bytes**, SHA256 `90c0277461594eef9d143c044725a5cb0b8a0ad9e7b4958d5a4f700f389a81bd`. Normal preflash checks exclude both synthetic PCM fixtures, preserve strict TLS CA/name/date verification, compile private logging out of all five transport units, exclude the actual device credential from the image, and preserve NVS. Exact normal write files/configuration/manifest are archived under `recovery/2026-11-18-phase-5c/built-standalone/`.

The private pre-standalone full flash is `recovery/2026-11-18-phase-5c/before-standalone-full-flash.bin`, 8,388,608 bytes, SHA256 `7f78a1f2632da24f51c66aac466cb3c47fc65fb6604d0e81d1cc971c29507e42`; its application matches the verified Phase 5b image byte-for-byte. Earlier backups remain intact. The passing diagnostic app is archived separately (`built-standalone-fixture/`, SHA256 `079ec1a96a00a615ff87ca2a2ef7cb77ce298e44e03bda52d7e3077dd9166f8b`). Recovery files remain private because full flash includes saved credentials. [Recovery procedure](BUILD_AND_FLASH.md).
