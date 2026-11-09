# ESP32 Companion

Step 1 establishes a recoverable build of the supplied TechTalkies Xiaozhi firmware on the development XIAO ESP32-S3 at **COM26**.

**First smoke test passed on 2026-11-09:** source built and flashed successfully; serial startup, eyes, startup sound, and English wake response verified. Original flash and both build variants are saved locally for recovery.

**Local diagnostic mode is available:** independent face/tone/microphone-level tests and repeated English wake acknowledgment, with Wi-Fi off at boot. [Diagnostic commands and test procedure](docs/DIAGNOSTICS.md).

**Phase 2a adds explicit local states:** idle, wake detected, timed listening, local cue, and idle recovery. [State-cycle verification](docs/LOCAL_STATES.md). Bounded microphone-segment capture is the next increment.

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

Cloudflare and Gemini work is deferred until the hardware baseline passes. The baseline retains the original network, activation, and protocol behavior.

Based on [TechTalkies/Xiaozhi-for-XiaoESP32S3](https://github.com/TechTalkies/Xiaozhi-for-XiaoESP32S3), preserving the supplied licenses and attribution.
