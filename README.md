# ESP32 Companion

Step 1 establishes a recoverable build of the supplied TechTalkies Xiaozhi firmware on the development XIAO ESP32-S3 at **COM26**.

- Working ESP-IDF project: `Source/xiaozhi-esp32-2.2.2/`.
- Supplied reference: `_Reference_Code/` (untouched and excluded from Git).
- Toolchain: installed **ESP-IDF v5.5.2**, matching this source's minimum requirement.
- Hardware: probed **8 MB flash / 8 MB embedded PSRAM**, SSD1306 128x64 profile.
- [Build, flash and recovery](docs/BUILD_AND_FLASH.md)
- [Hardware and wiring](docs/HARDWARE.md)
- [Observed test results](docs/TEST_LOG.md)
- [Source provenance](docs/BASELINE.md)

Open `ESP32_Companion.code-workspace` in VS Code. Select the **Firmware** folder for ESP-IDF extension commands. From the repository root:

```powershell
.\tools\idf.ps1 -Action build
```

Build/flash/monitor tasks are also available through **Terminal > Run Task**. Read the recovery procedure before the first flash.

Cloudflare and Gemini work is deferred until the hardware baseline passes. The baseline retains the original network, activation, and protocol behavior.

Based on [TechTalkies/Xiaozhi-for-XiaoESP32S3](https://github.com/TechTalkies/Xiaozhi-for-XiaoESP32S3), preserving the supplied licenses and attribution.
