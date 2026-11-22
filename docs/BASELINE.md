# Source provenance

This page records the original baseline. Current firmware source is `Source/esp32-companion/`, and new artifacts use `esp32_companion` names. Historical names and upstream URLs below remain as provenance. The current specialized implementation is documented in [PLATFORM_SPECIALIZATION.md](PLATFORM_SPECIALIZATION.md); `sdkconfig.hardware` replaces the historical baseline overlay and the original cloud runtime has been removed.

The supplied folder is an archive without Git metadata. It remains untouched under `_Reference_Code/Xiaozhi-for-XiaoESP32S3-master/`.

Upstream `master` fetched on 2026-11-09:

```text
https://github.com/TechTalkies/Xiaozhi-for-XiaoESP32S3.git
42359985ff3992641dd47411b8d5f1f6c9f754f7
```

Local branch `upstream-reference` and tag `upstream-reference-4235998` preserve that fetched commit. The imported supplied source has its own `baseline` branch and `supplied-source-baseline` tag. Development is on `work/step-1-baseline`.

Comparing the supplied firmware source against the fetched upstream tree found one difference: `main/Kconfig.projbuild` already defaults OLED to SSD1306 128x64 instead of 128x32. That supplied change is preserved. Do not describe the archive as a proven checkout of the fetched commit.

Working firmware was copied into the development source tree, now named `Source/esp32-companion/`. No GPIO, driver, activation, cloud protocol, or application changes are needed for the first baseline build. `sdkconfig.baseline` explicitly selects the real board symbol, 8 MB flash, the 8 MB partition CSV, OLED 128x64, and the existing AFE wake configuration. It overrides conflicting generic defaults without changing the reference.

The original fork ignores `dependencies.lock`. This working copy now retains the successfully resolved lock so future builds use the same component versions. The initial build/flash passed with IDF v5.5.2; identity and hardware observations are recorded in `TEST_LOG.md`.
