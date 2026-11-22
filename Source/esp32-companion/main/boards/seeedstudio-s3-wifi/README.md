# Fixed companion hardware

This implementation targets the verified XIAO ESP32-S3 with 8 MB flash and 8 MB PSRAM, SSD1306 128x64 OLED, separate I2S microphone and MAX98357A speaker buses. Pin assignments are in `board_config.h`; see the repository's `docs/HARDWARE.md` and `docs/BUILD_AND_FLASH.md`.

The board initializes only its OLED and audio codec. The companion workflow manages Wi-Fi directly with saved credentials and uses local wake detection; it has no generic board selection, alternate display driver, battery peripheral, camera, LED state machine, or GPIO press-to-talk controller.

The OLED keeps the existing idle, listening and speaking face animation and a short status label. Its English status text uses LVGL's built-in Montserrat 14 font.

Adapted from the supplied XIAO ESP32-S3 hardware implementation. Original licenses remain at the firmware and repository roots; upstream attribution is recorded in `docs/BASELINE.md`.
