# Build, flash and recovery

Updated 2026-11-19 for the specialized companion. Original baseline firmware and historical results remain in recovery files and Git history; the original cloud runtime is no longer a build profile.

## Build

ESP-IDF **v5.5.2** is installed at `C:/esp/v5.5.2/esp-idf`. From the repository root:

```powershell
.\tools\idf.ps1 -Action build
```

The default profile is **standalone**. Build outputs default to `%LOCALAPPDATA%/ESP32_Companion/build-standalone/`, outside Dropbox. Use `-BuildRoot PATH` for another output parent. Source stays in `Source/esp32-companion/`. Each build owns its generated configuration at `build-<profile>/sdkconfig`, outside Dropbox. `%LOCALAPPDATA%` is persistent local application data, not the temporary directory. VS Code build paths point to the same default standalone build. If configuration was interrupted, run `-Action reconfigure` with the same profile and BuildRoot before building again.

A missing build folder is regenerated from the checked-in defaults and pinned dependency lock. No old object files or generated source sdkconfig are needed. The verified flash configuration selects QIO; ESP-IDF emits a DIO image header before the bootloader enables quad mode. The generated flash arguments remain authoritative.

The fixed configuration uses `sdkconfig.defaults`, `sdkconfig.defaults.esp32s3`, and `sdkconfig.hardware`, followed by the selected profile's files in `config/`. It requires ESP32-S3, 8 MB flash, the existing `partitions/v2/8m.csv`, 8 MB PSRAM, and the bundled English `wn9_hiesp` wake model. Wiring and OLED geometry are compiled directly from `board_config.h`. No board/display/language selection menu remains.

Useful reduced diagnostic profiles remain: `diagnostics`, `session`, `gemini`, `voice`, `microphone`, `question`, `wake`, and `followup`. These support the existing calibrator and self-paced test tools. `-FixtureCheck` is restricted to question/followup/standalone and embeds only the public synthetic test PCM; these images do not automatically upload room microphone audio.

## Clean rebuilds and retained files

The 2026-11-19 cleanup reduced the Dropbox workspace from 3.98 GB to approximately 1.20 GB. It removed all eight old in-source build trees, the previous external build trees, generated source sdkconfig files, npm download cache, the Node ZIP installer, and old host-test executables. Active managed components, portable Node and Worker node_modules remain because they are development dependencies. `dependencies.lock` fixes the firmware package versions.

A fresh build folder regenerates sdkconfig from the versioned defaults. The preflash check reads the chosen build's actual sdkconfig and binary paths from its metadata. To bypass compiler caching explicitly:

```powershell
.\tools\idf.ps1 -Action build -NoCache
```

`-NoCache` forces compilation without ccache; it does not delete a build directory. For a clean directory on a future occasion, select a new `-BuildRoot` or remove that profile's generated build folder first. Avoid keeping many duplicate build roots.

Keep source/Git history, hardware/profile defaults, dependency locks, credentials, real calibration/test evidence, the original full-flash backup, and the last physically verified firmware with its symbols/configuration. Those historical recovery artifacts remain under `recovery/`. There is no need to retain the old compiler object/cache trees alongside them. The newly rebuilt image stays in persistent LocalAppData until the next build or an explicit cleanup. The current build tree is approximately 206 MB; 4.29 GB of obsolete files were deleted across both locations, for approximately 4.09 GB net reduction after rebuilding.

The latest fresh cache-disabled `esp32_companion` standalone build and preflash checks pass, with 155 compiled policy assertions and a 1,935,552-byte normal app (SHA256 `9da0248507080060586e1a00d7e224b6dd60b59ce5f297cbcd1efbb01853c0d0`). No device flash was performed. Original recovery/reference files were verified unchanged by hash; detailed cleanup evidence is under `logs/clean-slate-*`.

## Flash

Close all serial clients first. The wrapper verifies COM26 and its expected MAC before any profile is flashed:

```powershell
python .\tools\check_standalone_build.py
.\tools\idf.ps1 -Action flash -Port COM26
.\tools\idf.ps1 -Action monitor -Port COM26
```

For a custom BuildRoot, pass the actual build directory to `check_standalone_build.py --build PATH`. Exit the IDF monitor with **Ctrl+]**. The verification tool also checks the historical recovery files, secure TLS configuration, fixture exclusion, compiled policy assertions and absence of the private device credential.

Current project identity is `esp32_companion`; the source directory is `Source/esp32-companion/`. Historical recovery artifacts keep their original filenames and are resolved from their saved metadata.

Expected outputs include `esp32_companion.bin`, `esp32_companion.elf`, `esp32_companion.map`, bootloader, partition table, initial OTA metadata, `generated_assets.bin`, and `flasher_args.json`. The wake-only asset image is generated locally and flashed with the app. Flashing only the app while retaining unrelated old assets is not the normal workflow. The existing OTA partitions are retained for recovery/layout compatibility; no firmware OTA client remains.

## Recovery

The original full 8 MB flash backup remains under `recovery/2026-11-09-COM26/`. Do not overwrite it or use a blanket erase. Normal generated flash commands preserve the NVS region containing Wi-Fi/device credentials. The generated `flasher_args.json` is authoritative.

| Address | Region |
|---|---|
| `0x000000` | bootloader |
| `0x008000` | partition table |
| `0x009000` | NVS, preserved by normal flash |
| `0x00D000` | OTA metadata |
| `0x00F000` | PHY data |
| `0x020000` | application in OTA0 |
| `0x310000` | OTA1, retained layout |
| `0x600000` | local wake-model assets |

For an exact rollback on this same device, verify the saved checksum and restore its full backup with the installed ESP-IDF Python:

```powershell
& 'C:\Espressif\tools\python\v5.5.2\venv\Scripts\python.exe' -m esptool --chip esp32s3 --port COM26 --baud 460800 write_flash 0x0 .\recovery\2026-11-09-COM26\original-full-flash.bin
```

The full backup restores the original NVS and OTA selection too. If the port vanishes, hold BOOT, press/release RESET, release BOOT and check Windows for the new COM number. See [observed tests](TEST_LOG.md) and [source provenance](BASELINE.md) for the original baseline evidence.
