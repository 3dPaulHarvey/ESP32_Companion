# Build, flash and recover

Use the development unit on **COM26**. Leave the original working robot separate.

## Pinned setup

- ESP-IDF **v5.5.2**, clean installed Git tag at `C:\esp\v5.5.2\esp-idf`.
- Activation: `C:\Espressif\tools\Microsoft.v5.5.2.PowerShell_profile.ps1`.
- Python: `C:\Espressif\tools\python\v5.5.2\venv\Scripts\python.exe`.
- ESP-IDF VS Code extension: **2.3.0**.
- Firmware root: `Source/xiaozhi-esp32-2.2.2/`.
- ESP32-S3, quad 8 MB flash, octal PSRAM, SSD1306 128x64.

The board README's IDF 5.4.1 instructions are stale for this archive: `main/idf_component.yml` requires `>=5.5.2`. Its board `config.json` also has invalid board/flash symbols. Use the explicit `sdkconfig.baseline` profile rather than that release recipe.

The current development profile selects bundled English **“Hi, ESP”** (`CONFIG_SR_WN_WN9_HIESP=y`) and disables the generic Chinese wake model. Say “Hi, E-S-P” with the three letters spoken separately. This is an existing ESP-SR model, documented in [Espressif's English example](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/getting_started/readme.html). The original successful Chinese-model build is tagged `step-1-baseline-flashed` and archived in `recovery/.../built-baseline/`.

To apply this change to an already-generated Chinese `sdkconfig`, use `idf.py menuconfig` and select the Hi,ESP WakeNet9 model while deselecting `wn9_nihaoxiaozhi_tts`. Default files seed fresh configurations; they do not override existing selections. A normal build/flash must include regenerated assets. Verify boot logs show `AfeWakeWord: Model 0: wn9_hiesp`.

## Exact commands

From the repository root, PowerShell:

```powershell
.\tools\idf.ps1 -Action build
.\tools\idf.ps1 -Action flash -Port COM26
.\tools\idf.ps1 -Action monitor -Port COM26
```

The wrapper activates the installed v5.5.2 environment, enters the firmware root and runs this equivalent command (replace `build` with `-p COM26 flash` or `-p COM26 monitor`):

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.2.PowerShell_profile.ps1'
Set-Location .\Source\xiaozhi-esp32-2.2.2
idf.py -DIDF_TARGET=esp32s3 '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.baseline' build
```

Exit the IDF monitor with **Ctrl+]**. Close monitors before backup/flash. Defaults seed a fresh `sdkconfig`; they do not overwrite an existing user's menuconfig choices. Before flashing, check the generated `sdkconfig` still selects `CONFIG_BOARD_TYPE_SEEEDSTUDIO_S3_WIFI=y`, `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`, `partitions/v2/8m.csv`, and `CONFIG_OLED_SSD1306_128X64=y`.

Expected build results: `build/xiaozhi.bin`, `bootloader/bootloader.bin`, `partition_table/partition-table.bin`, `ota_data_initial.bin`, `generated_assets.bin`, and `flasher_args.json`. Expected flash output includes `Hash of data verified` and a reset. Flashing the assets is necessary for the wake model.

The original application can automatically install an OTA update once Wi-Fi/activation succeeds. Record the boot version and any OTA event when checking the baseline; an update can replace the locally flashed image. No cloud behavior has been altered for this first build.

## Before the first write

Save an exact full-flash backup, checksum, and decoded partition table under `recovery/2026-11-09-COM26/`. This directory is Git-ignored because flash/NVS may contain device and Wi-Fi credentials. The initial read command is:

```powershell
& 'C:\Espressif\tools\python\v5.5.2\venv\Scripts\python.exe' -m esptool --chip esp32s3 --port COM26 --baud 460800 read_flash 0x0 0x800000 .\recovery\2026-11-09-COM26\original-full-flash.bin
Get-FileHash .\recovery\2026-11-09-COM26\original-full-flash.bin -Algorithm SHA256
```

Confirm the backup is **8,388,608 bytes** and matches the saved checksum. Never overwrite that original backup. Preserve the supplied `Firmware/` binaries and flash map as a second recovery option.

## Flash map

| Address | File / region |
|---|---|
| `0x000000` | bootloader |
| `0x008000` | partition table |
| `0x009000` | NVS, retained by normal build flashing |
| `0x00D000` | initial OTA metadata |
| `0x00F000` | PHY data |
| `0x020000` | firmware in OTA0 |
| `0x310000` | OTA1 |
| `0x600000` | local assets and wake model |

The generated `build/flasher_args.json` is authoritative for a successful build. Do not use a blanket erase.

## Smoke-test checklist

Monitor at 115200 baud. Expected initialization markers include `Init SeeedStudioS3WifiBoard`, PSRAM discovery, `NoAudioCodec`, and application state logs. The baseline may enter Wi-Fi provisioning or activation; that is expected original behavior and does not prove independent hardware diagnostics.

For a bounded 30-second capture from the repository root:

```powershell
python .\tools\capture_serial.py --port COM26 --reset --seconds 30 --name baseline-boot
```

This resets the device and prints selected hardware/state lines. Full output stays in the ignored `logs/` directory. Exact markers include `NoAudioCodec: Simplex channels created`, `AudioCodec: Audio codec started`, and `State: <old> -> <new>`. Loaded wake models log `AfeWakeWord: Model ...` only after assets have been applied.

- [ ] No reboot loop or watchdog/panic; 8 MB PSRAM initializes.
- [ ] OLED shows animated eyes and blinking.
- [ ] Speaker cue is audible and clean.
- [ ] Mic responds to voice (requires diagnostic measurement or proven conversation).
- [ ] Wi-Fi connects/provisions; no credential values are copied into committed logs.
- [ ] Actual loaded wake phrase is identified and repeatable.

In the unchanged production application, `Wake word detected: ...` is logged only after opening the cloud audio channel succeeds. A local recognition can first appear as `idle -> connecting`, even if the service then fails. Use that state transition as local detection evidence; absent speech alone does not prove a microphone failure.

Record measured results in `TEST_LOG.md`; source inspection alone does not pass these checks. Network-independent diagnostics belong to the next small implementation after this baseline is reviewable.

## Exact rollback

Close the serial monitor. Check the backup checksum against `original-full-flash.sha256`, then restore all 8 MB:

```powershell
& 'C:\Espressif\tools\python\v5.5.2\venv\Scripts\python.exe' -m esptool --chip esp32s3 --port COM26 --baud 460800 write_flash 0x0 .\recovery\2026-11-09-COM26\original-full-flash.bin
```

This restores that device's original bootloader, partition table, OTA selection, app, assets, and NVS. Use this backup only for the same development unit. If the port vanishes, hold BOOT, press/release RESET, release BOOT, then inspect Windows for the new COM number and substitute it. Power-cycle after restoring.

If the saved device image is unavailable, the untouched reference `Firmware/flash_code.txt` supplies a separate factory-style multi-file map. Its COM24 is obsolete; substitute the connected port. A multi-file factory flash does not restore the saved device NVS/OTA state as a full device backup does.
