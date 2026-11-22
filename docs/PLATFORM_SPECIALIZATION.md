# Platform specialization

2026-11-19. Target: the verified XIAO ESP32-S3, 8 MB flash/PSRAM, SSD1306 128x64, separate I2S microphone/MAX98357A buses, English Hi ESP wake, our Cloudflare authentication service and direct Gemini PCM conversations.

## Retained workflow

Saved Wi-Fi at boot; local wake detection; a four-second question capture; secure authenticated session access; two bounded answers on one connection; local quiet follow-up discard; READY and wake recovery. Calibration, USB diagnostics, synthetic fixtures, session/transport guards and existing host tests remain useful for developing this device. TLS partial-write recovery and failure/cancellation handling are preserved.

## Removed code

- Original Application event loop, upstream activation/server discovery, OTA update client, MQTT/UDP and original WebSocket voice protocols, MCP tools and the redundant device state machine.
- Alternative codec drivers, generic speech processors/AEC, Opus upload/encode/test queues, wake-audio upload buffers, non-S3/custom wake paths, input resampling and ineffective codec power toggles.
- Board factory inheritance/registration, cellular/Ethernet/RNDIS, cameras, battery/charger/sleep controllers, generic Wi-Fi board startup, acoustic/Bluetooth provisioning, accessories and unused button/LED handlers.
- LCD/Emote display implementations, themes, emoji/GIF/JPEG helpers, alternate OLED sizes, multilingual strings/audio and unused startup/activation sounds.
- Unused partition tables and target-chip defaults, generic release/asset/conversion tools, upstream issue templates and generic CI/docs. The fixed asset packer contains only wn9_hiesp and retains the existing flash format.
- The Settings abstraction: audio loads its saved NVS volume directly. No volume setter had an active caller.

## Intentional details

The OLED face geometry and 60 ms animation remain. Its English status font now uses LVGL's built-in Montserrat 14, removing the upstream fonts/emoji package. Standalone never installed the original button listeners; removing those objects also removes the duplicate GPIO3 registration. Wake, serial diagnostic controls and the active USB clients remain the controls.

The build rejects non-S3 targets and mismatched flash layouts. Standalone is the default, and artifacts default outside Dropbox. Active firmware source is `Source/esp32-companion/`, the CMake project is `esp32_companion`, and new build artifacts are `esp32_companion.bin`, `esp32_companion.elf`, and `esp32_companion.map`. Build tools and IDE paths use these names. The partition layout and both supplied license files are preserved. Upstream attribution, historical observations, and immutable recovery/reference archive names retain their original identities; active implementation names use ESP32 Companion.

Third-party libraries remain where they directly provide needed behavior: ESP-IDF, ESP-SR/AFE, LVGL/its port, OGG popup decoding/resampling, and saved station Wi-Fi. Managed dependencies are not edited in place. In particular, the Wi-Fi dependency still contains its own generic AP configuration implementation; our runtime only starts station mode. Replacing that dependency with a tested station-only implementation is a separate network behavior change.

## Validation

| Measure | Before | Specialized |
|---|---:|---:|
| Main C/C++/header files | 161 | 45 |
| Main C/C++/header lines | 25,923 | 5,655 |
| Main CMake lines | 1,111 | 106 |
| Project Kconfig lines | 939 | 88 |
| Normal app bytes | 2,599,264 | 1,961,520 |
| Resolved dependencies, including IDF | 64 | 9 |

The normal app is 24.5% smaller, with 36% of the existing application partition free. Its SHA256 is `fd48017dfcb29fbfeb50cd82d9eed23230000e5430924ec552c0afd9b85f1819`. The wake-only asset image is 291,288 bytes; independent decoding of its outer table and ESP-SR model headers confirms that every bundled model file matches the installed wn9_hiesp source bytes.

Passed: 84 host/tool tests; all nine existing C++ policy suites and both local runtime suites; full ESP-IDF standalone, synthetic-fixture and reduced-diagnostics builds; both existing preflash checks (155 compiled assertions, secure TLS, private transport logs excluded, credential absent, correct fixtures and preserved NVS/recovery). Fixture build SHA256: `2a5b0ea2378607e0ff9ea0afe0a88b4cff6ea711bcfad85af889010b5f2b8bb1`.

Builds use `%LOCALAPPDATA%/ESP32_Companion/specialization/`. New preflash evidence is saved separately as `logs/platform-specialization-normal-preflash.json` and `logs/platform-specialization-fixture-preflash.json`. Earlier reviewed result files and recovery images are preserved. The preflash tool now accepts `--report PATH` for this purpose.

This source pass has not flashed the device. Physical wake, sound, display and reconnection need a smoke test of the rebuilt image; existing physical results describe the previous image.


## Clean-slate rebuild follow-up

A subsequent cleanup on 2026-11-19 removed the old in-source and external build trees and disposable caches. The `specialization/` build paths above are historical test locations and have now been removed. Current builds use `%LOCALAPPDATA%/ESP32_Companion/build-standalone/`, including their generated sdkconfig. The checked-in hardware/profile defaults and dependency lock are sufficient to recreate them. [Current build and cleanup procedure](BUILD_AND_FLASH.md).
