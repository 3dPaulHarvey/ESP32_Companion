# ESP32 Companion

A standalone voice companion for one fixed XIAO ESP32-S3: local **Hi, E-S-P** wake detection, a 16 kHz I2S microphone, a 24 kHz speaker, and an animated SSD1306 OLED face. Saved Wi-Fi starts at boot. Our Cloudflare Worker authenticates the device and issues temporary Gemini credentials; the ESP32 exchanges bounded PCM directly with Gemini.

The normal workflow connects while recording, streams accepted microphone audio and reply PCM, and ends a question after 600 ms of detected silence. It waits up to four seconds for speech, caps capture at ten seconds, and keeps a short connection ready between nearby wakes. Sessions permit eight answers, 30 seconds idle or two minutes total. Quiet is discarded locally. It works from a separate USB power supply without a PC arm or heartbeat. Capture shift remains 15; wake conversion remains 12.

**Platform specialization, 2026-11-19:** the upstream application, activation/OTA, MQTT, MCP and generic hardware paths have been removed. Standalone is now the default build. Local diagnostics, calibration, recovery checks and synthetic test profiles remain because they support this implementation. [Changes and validation](docs/PLATFORM_SPECIALIZATION.md).

```powershell
.\tools\idf.ps1 -Action build
```

Generated builds and their sdkconfig default to `%LOCALAPPDATA%/ESP32_Companion/build-standalone/`, outside Dropbox in persistent local storage. Firmware source is in `Source/esp32-companion/`; new builds produce `esp32_companion.bin`, `esp32_companion.elf`, and `esp32_companion.map`. [Build, flash and recovery](docs/BUILD_AND_FLASH.md).

- [Hardware and wiring](docs/HARDWARE.md)
- [Standalone behavior and physical test procedure](docs/ESP32_STANDALONE.md)
- [Response performance audit and optimization plan](docs/PERFORMANCE.md)
- [Microphone calibrator](docs/MICROPHONE_CALIBRATOR.md)
- [Upload recovery and its observed limits](docs/UPLOAD_RECOVERY.md)
- [Cloudflare Worker](docs/CLOUD_WORKER.md) and [device authentication](docs/DEVICE_AUTH.md)
- [Cloudflare and Gemini free-tier usage](docs/CLOUD_USAGE.md)
- [Test history](docs/TEST_LOG.md) and [source provenance](docs/BASELINE.md)

## Open the test programs

Double-click these CMD launchers in the project folder. The programs and their shared modules live together in `tools/`; none depends on files on the Desktop.

| Test | CMD launcher | Program |
| --- | --- | --- |
| Microphone calibrator | [Open Microphone Calibrator.cmd](Open%20Microphone%20Calibrator.cmd) | `tools/microphone_calibrator.py` |
| Follow-up conversation | [Open Robot Conversation Test.cmd](Open%20Robot%20Conversation%20Test.cmd) | `tools/robot_conversation_test.py` |
| Spoken question | [Open Robot Question Test.cmd](Open%20Robot%20Question%20Test.cmd) | `tools/robot_question_test.py` |
| Standalone | [Open Robot Standalone Test.cmd](Open%20Robot%20Standalone%20Test.cmd) | `tools/robot_standalone_test.py` |
| Wake question | [Open Robot Wake Test.cmd](Open%20Robot%20Wake%20Test.cmd) | `tools/robot_wake_test.py` |
| Live observer | [Open Robot Live Observer.cmd](Open%20Robot%20Live%20Observer.cmd) | `tools/robot_live_observer.py` |

The five Desktop icons are Windows shortcuts to these same project programs. Copies are saved in `tools/desktop-shortcuts/`, so the Desktop shortcuts can be deleted. Continue opening the tests with the CMD launchers above. The saved shortcuts retain this computer's original absolute paths; the CMD launchers locate the programs relative to the project folder.

## Offline checks

```powershell
python -m unittest discover -s tools -p 'test_*.py'
python tools/check_cpp_policies.py
```

The C++ runner requires native g++ or clang++. Worker runtime checks use `tools/worker.ps1 -Action test`.

Based on [TechTalkies/Xiaozhi-for-XiaoESP32S3](https://github.com/TechTalkies/Xiaozhi-for-XiaoESP32S3) and Xiaozhi ESP32. Supplied root and firmware licenses and attribution are preserved. The untouched supplied reference remains under `_Reference_Code/` and saved recovery images remain under `recovery/`, both excluded from Git.
