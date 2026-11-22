# ESP32 Companion firmware

This is the specialized firmware for the project's single XIAO ESP32-S3 configuration. See the [project README](../../README.md), [hardware wiring](../../docs/HARDWARE.md), [build instructions](../../docs/BUILD_AND_FLASH.md), and [specialization record](../../docs/PLATFORM_SPECIALIZATION.md).

The firmware project is `esp32_companion`, producing `esp32_companion.bin`, `.elf` and `.map` artifacts. The source lives in `Source/esp32-companion/`. Only the fixed OLED, simplex I2S and English AFE wake implementation is built.

Original MIT copyright and permission notices remain in [LICENSE](LICENSE). The supplied project-level license remains at the repository root. [Upstream provenance](../../docs/BASELINE.md), historical source in Git, and the untouched `_Reference_Code/` tree preserve the source history.
