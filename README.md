# ESP32 Companion

Step 1 establishes a recoverable build of the supplied TechTalkies Xiaozhi firmware on the development XIAO ESP32-S3 at **COM26**.

**First smoke test passed on 2026-11-09:** source built and flashed successfully; serial startup, eyes, startup sound, and English wake response verified. Original flash and both build variants are saved locally for recovery.

**Local diagnostic mode is available:** independent face/tone/microphone-level tests and repeated English wake acknowledgment, with Wi-Fi off at boot. [Diagnostic commands and test procedure](docs/DIAGNOSTICS.md).

**Phase 2b adds bounded local microphone capture:** wake, two seconds of PCM, an end cue, and idle recovery. Capture and cancellation passed automated checks; three voice-triggered captures completed in serial. The user confirms the updated physical behavior works in a follow-up test on 2026-11-17. Microphone headroom calibration remains open before voice streaming. [Current behavior and test procedure](docs/LOCAL_CAPTURE.md).

- Working ESP-IDF project: `Source/xiaozhi-esp32-2.2.2/`.
- Supplied reference: `_Reference_Code/` (untouched and excluded from Git).
- Toolchain: installed **ESP-IDF v5.5.2**, matching this source's minimum requirement.
- Hardware: probed **8 MB flash / 8 MB embedded PSRAM**, SSD1306 128x64 profile.
- Development wake phrase: **“Hi, ESP”** (say “Hi, E-S-P”). The first Chinese-model build is preserved at tag `step-1-baseline-flashed`.
- [Build, flash and recovery](docs/BUILD_AND_FLASH.md)
- [Hardware and wiring](docs/HARDWARE.md)
- [Observed test results](docs/TEST_LOG.md)
- [Source provenance](docs/BASELINE.md)

Open `ESP32_Companion.code-workspace` in VS Code. Select the **Firmware** folder for ESP-IDF extension commands. From the repository root:

```powershell
.\tools\idf.ps1 -Action build
```

Build/flash/monitor tasks are also available through **Terminal > Run Task**. Read the recovery procedure before the first flash.

To build or flash local diagnostics, add `-Profile diagnostics` to the wrapper command. The default profile remains `baseline`.

**Phase 3a health endpoint is verified locally and online:** runtime tests, packaging and hosted HTTPS success/error checks pass. [Run the Worker and view the next steps](docs/CLOUD_WORKER.md). [Beginner architecture overview](docs/CLOUD_OVERVIEW.md). The baseline retains the original network, activation, and protocol behavior.

**Phase 3b device authentication is verified locally and online:** missing/wrong credentials are rejected and the current local credential succeeds after synchronizing the Production Secret. [Device authentication procedure](docs/DEVICE_AUTH.md).

**Phase 3c shared request limits are verified locally and online:** the device gets 10 control requests per rolling minute, excess requests receive 429/retry responses, and recovery passes. Wrangler is authorized and the SQLite-backed counter is deployed. [Rate-limit deployment and checks](docs/RATE_LIMITS.md).

**Phase 3d Gemini session access is verified online:** the permanent key is a Production Worker Secret, authenticated temporary-token issuance passes, and Gemini 3.8 Live accepts the desktop constrained-WebSocket setup. All 20 local runtime test groups pass. That initial connection check sent no prompt or microphone audio. [Key setup, session contract and repeatable check](docs/GEMINI_SESSIONS.md).

**First desktop spoken reply is verified:** Gemini returned 2.74 seconds of valid 24 kHz mono PCM without clipped samples. Windows playback completed, and the user confirmed clear speech at normal speed. The test sent a fixed text greeting request and used no microphone. [Replay and results](docs/DESKTOP_VOICE.md).

**Phase 3f prerecorded audio exchange is verified:** a 16 kHz synthetic question received the correct spoken answer at 24 kHz, with clear playback confirmed by the user. Six offline desktop-client tests pass. [Audio question, replay and results](docs/DESKTOP_AUDIO.md).

**Phase 4a ESP32 session requests are verified:** private device-credential provisioning and HTTPS rejection/success checks pass, and local capture/cue recovery works during a request. [Session-only profile and results](docs/ESP32_SESSIONS.md).

**Phase 4b direct ESP32 Gemini setup is verified:** six consecutive final connections verify setup acknowledgment and clean closure; concurrent local capture, offline recovery and the user's eyes/wake/listening/cue check pass. [Setup profile and results](docs/ESP32_GEMINI_SETUP.md).

**Phase 4c Gemini speech through the robot is verified:** manual `v` receives a fixed greeting, closes Gemini, plays bounded PCM through the existing speaker, and returns to READY. The user confirms clear speech at normal speed. Repeated output, busy guards and device-side Wi-Fi-loss recovery pass. [Speaker checkpoint](docs/ESP32_VOICE.md).

**Phase 4d microphone measurements are reviewed and applied:** three quiet + three speech samples had zero speech clipping at shift 14, 12.72 dB level contrast, and confirmed four-second phrase fit. Capture shift 15 adds headroom; wake stays at 12. The Desktop **ESP32 Microphone Calibrator** remains available for later adjustments. [Calibration and saved data](docs/MICROPHONE_CALIBRATOR.md).

**Phase 4e real microphone question/answer is verified:** the user confirms a correct spoken result. The saved run verifies a complete four-second upload, exactly 3.8 seconds of reply playback, zero input/output clipping and READY recovery. **Robot Spoken Question Test** on the Desktop remains available for repeat tests, with automatic numeric-result saving. That checkpoint used the `question` profile with explicit Start; the successor wake profile is described below. [Results and repeat procedure](docs/ESP32_QUESTIONS.md). [Roadmap progress](docs/ROADMAP_STATUS.md).

The first live failures exposed a fast device clock and unsupported audio-input metadata. Both are corrected. Connect validates Gemini setup before recording. Prerecorded and real microphone turns now pass.

**Phase 5a armed wake question is verified:** three consecutive physical wake/question/answer/READY cycles passed, with correct clear answers confirmed by the user and zero input/output clipping. **Robot Wake Question Test** on the Desktop remains available: Connect, **Arm next wake**, say **Hi, E-S-P**, then ask the question when green appears. Each arm permits one question/reply and results autosave. That checkpoint used the `wake` profile. [Results and self-paced wake test](docs/ESP32_WAKE.md).

**Phase 5b bounded follow-up is verified:** the quiet check and two full wake/question/answer/follow-up/answer/quiet/READY trials passed, with five correct clear answers confirmed by the user and zero input/output clipping. Both full conversations retain context on one connection; a fresh wake after closure passes. **Robot Follow-up Conversation Test** remains available for self-paced repeats. COM26 runs `followup` with two answers per arm; boot stays offline/unarmed. The next checkpoint is standalone wake operation. [Results and repeat procedure](docs/ESP32_FOLLOWUP.md).

Based on [TechTalkies/Xiaozhi-for-XiaoESP32S3](https://github.com/TechTalkies/Xiaozhi-for-XiaoESP32S3), preserving the supplied licenses and attribution.
