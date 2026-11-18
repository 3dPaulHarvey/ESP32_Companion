# Self-paced robot microphone check

Phase 4d, 2026-11-18: the standalone Windows calibrator and its COM26 firmware are ready. **User-run quiet/speech calibration is pending.** Automated integration data and demo data do not count as human calibration.

We are checking whether normal speech clips, how far speech levels rise above room noise, and whether a four-second listening window fits a short question. Earlier two-second tests were difficult to coordinate; the user missed some windows, so those attempts cannot establish normal-speech headroom. This step sends no microphone audio to Gemini.

## Open and run

1. Connect the development robot on **COM26** and close serial monitors. Double-click **ESP32 Microphone Calibrator** on the Desktop, or **Open Microphone Calibrator.cmd** in the project folder.
2. Click **Connect robot**. It verifies the development MAC, checks the microphone firmware, turns Wi-Fi off if necessary, and temporarily pauses wake recognition while the app owns calibration. Connecting never starts a sample.
3. Follow three **Quiet baseline** samples. Pause background audio, click **Start this sample** when ready, and stay silent during the four-second green **STAY QUIET** window. The robot's cue marks the end. Click **Keep this sample** if you followed the instructions; otherwise choose **Redo this sample**.
4. Follow three **Normal speech** samples. Sit 1–2 feet from the robot microphone. Click Start, wait for the countdown and green **SPEAK NOW**, and say **“What is two plus two?”** once at normal volume. Speak to the robot, not the PC. Keep only if the sentence fit before the cue. Clipping is useful evidence: do not redo a correctly performed sample just to obtain a lower peak.
5. Optionally run the wake check: click Start, say **“Hi, E-S-P”** when prompted, then say the question when **SPEAK NOW** appears. **Skip optional wake check** is also valid.
6. Click **Finish & close**. Tell Codex that the run is finished; Codex can read the saved results later without you copying numbers or keeping the program open.

Nothing advances to another recording automatically. Stop cancels the current attempt. Closing early saves a partial session; reopening resumes it. A finished but unreviewed sample still needs Keep or Redo. New session preserves the previous folder. A second launch cannot overwrite a running session.

## What the robot does

During a quiet or speech sample, the app starts the local listening cycle over USB. The robot holds LISTENING, collects four seconds, plays its local end cue, and returns to READY. It will not answer the question in this check. The green signal follows the firmware's recording event after its input-buffer discard, rather than a guessed host delay.

Normal wake recognition is paused between samples while connected to the calibrator, so spoken test material cannot trigger extra cycles. The optional wake check briefly restores recognition. Closing releases COM26 and rearms normal wake. If the app crashes or USB disconnects, the firmware's ten-second control lease expires and restores wake after capture cleanup. PC sleep/expired control requires reconnecting before another sample.

Outside the app, **Hi, E-S-P → four-second LISTENING → CUE → READY** remains local. Wi-Fi is off at boot. The prior manual Gemini greeting remains available separately.

## Saved evidence

Each attempt, review decision, cancellation and close is saved automatically in:

```text
logs/microphone-calibration/<local timestamp>_<session id>/session.json
logs/microphone-calibration/<local timestamp>_<session id>/summary.txt
logs/microphone-calibration/latest-session.json
logs/microphone-calibration/latest-complete.json
```

The complete pointer becomes available after three kept quiet and three kept speech samples. The optional wake result is reported separately. **Open saved folder** opens the current session. The project excludes logs from Git. JSON is replaced atomically; a failed save preserves the prior file and the app shows Retry save. UTC and local timestamps distinguish sessions.

Only numeric level summaries and the user's Keep/Redo decisions are retained: exact sample counts, RMS/peak/clipping, raw-input peak and headroom comparison, DC/AC levels, and four one-second summaries. No audio recording, transcript, credential or raw serial stream is written or uploaded. PCM exists only in the robot's capture worker and is wiped/freed before the result is published.

For later review, from the project root:

```powershell
python .\tools\review_microphone_calibration.py
python .\tools\review_microphone_calibration.py --latest-partial
```

The report compares median quiet-window RMS with each speech capture's strongest one-second RMS. This is **level contrast**, not a laboratory signal-to-noise measurement. Its suggested capture shift targets roughly 6 dB of peak headroom; it is advisory. The app never changes gain automatically. Near-full-scale raw words are flagged without assuming the installed microphone's exact ADC format. These statistics do not prove transcription or intelligibility; the later spoken question/answer test will check that.

## Firmware and developer verification

The separate **microphone** profile uses 64,000 samples / 128,000 bytes / 4,000 ms at 16 kHz mono. It reads 32-bit I2S words and uses a provisional right shift of **14** for question capture, providing four times the numeric headroom of the prior shift 12. **Wake recognition keeps its proven shift 12.** Warm-up discard remains 3,200 samples; worker/controller deadlines are 5.5/6 seconds. Earlier profiles retain their two-second capture and original conversion.

```powershell
.\tools\idf.ps1 -Action build -Profile microphone
.\tools\idf.ps1 -Action flash -Profile microphone -Port COM26
python .\tools\test_microphone_calibrator.py
python .\tools\test_microphone_calibrator_ui.py
```

The flash wrapper verifies the development MAC. Calibrator USB commands are limited to status, local capture/cancel, Wi-Fi off, and calibration hold/heartbeat/release. It cannot send provisioning or cloud commands. Demo preview (`python tools/microphone_calibrator.py --demo`) and UI tests save separately under ignored QA/test folders and never open USB.

Hardware integration passed recording events, exact counts/window summaries, heartbeat ownership beyond ten seconds, mid-capture cancellation, normal release, and abrupt-disconnect lease recovery with Wi-Fi off. The Tk event-loop check covers explicit starts, review/redo, six kept demo samples, optional skip, layout bounds and close during capture. Windows visual inspection could not run because the Computer Use native pipe was unavailable; the app was not visually certified through that tool. See [TEST_LOG.md](TEST_LOG.md) for build and recovery identities.

After the user runs this app, review the saved measurements, adjust capture headroom only if the data warrants it, and then proceed to one manual microphone question and Gemini reply. [Roadmap status](ROADMAP_STATUS.md).
