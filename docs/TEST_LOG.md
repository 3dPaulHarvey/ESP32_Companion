# Test log

## 2026-11-09 — Step 1 baseline

Development unit: COM26. Source tag: `supplied-source-baseline`; changes on `work/step-1-baseline`.

| Check | Observed result |
|---|---|
| Serial enumeration | COM26, Espressif USB Serial/JTAG; initially disconnected, reconnected by user |
| Chip probe | PASS: ESP32-S3 rev0.2, 40 MHz, embedded 8 MB AP_3v3 PSRAM |
| Flash probe | PASS: 8 MB, manufacturer c8/device4017, quad 3.3 V |
| Toolchain | PASS: installed ESP-IDF v5.5.2, clean tag; Python 3.13.5; esptool 4.13.0 |
| Reference preservation | PASS: reference untouched; supplied source committed/tagged separately |
| Recovery readback | PASS: 8,388,608-byte full image and SHA256 saved; partition table decoded |
| Recovery comparison | Bootloader/app/assets/table match supplied binaries; subsequent verification matches all flash outside mutable NVS |
| Source build | PASS: all 2,167 steps; app 2,614,000 bytes (`0x27e2f0`), 15% of app partition free; assets 624,371 bytes |
| Flash | PASS: `tools/idf.ps1 -Action flash -Port COM26`; five images written and hashes verified; no blanket erase |
| Boot identity | PASS: xiaozhi 2.2.2 / ESP-IDF v5.5.2; ELF prefix `da44e29d1`; no automatic OTA upgrade observed in capture |
| Runtime PSRAM | PASS: 8 MB at 80 MHz added to heap; 96 KB internal DMA reserve; flash running QIO |
| OLED driver | PASS: LVGL/OLED initialized; user confirms eyes work |
| Speaker | PASS startup cue: user confirms audible sound; separate tone/distortion measurements pending |
| I2S microphone | Initialization PASS: simplex codec/AFE input running; speech recognition/level measurement still pending |
| Wi-Fi | PASS connection/IP observed in private boot log; credentials omitted here; AP outage/retry not tested |
| Idle / local model | PASS startup: activating -> idle; `wn9_nihaoxiaozhi_tts` loaded, AFE task running, feed/fetch 512 samples |
| Wake recognition | User reports Chinese wake did not react, but does not speak Chinese; language/model test inconclusive; offline recognition not demonstrated |
| Runtime stability | No panic/assert/watchdog observed in 30-second boot capture; longer soak not tested |
| Heap | At idle: free SRAM 129,419/minimum 121,099 bytes; later 92,047/minimum 88,227 bytes |

Configuration findings: actual IDF minimum is 5.5.2; board metadata has malformed board/flash options; generic defaults select 16 MB. The explicit `sdkconfig.baseline` corrects the working build selection. No GPIO changes made.

Recovery image: `recovery/2026-11-09-COM26/original-full-flash.bin`, SHA256 `c4b87e30bbd56d71b3c36c8c93e5d81671bf06a5a0f4e9e34d17006633b6ba7e`. Image and checksums remain local/Git-ignored. Secure boot and flash encryption report disabled. After the original app was allowed to boot, a full-image verification differed; header readback localized 6,331 changed bytes to NVS only. Bootloader/padding/table/OTA/PHY regions were unchanged, and digest verification of the entire `0x20000..0x800000` range passed. The original backup was preserved unchanged.

Built images, generated configuration, dependency lock and flash map are archived under `recovery/2026-11-09-COM26/built-baseline/` with a SHA256 manifest. App SHA256: `f780f306083f7b23929a5884881fbf916e95586052e056fd29256f1e3a02d4bb`. Build and flash output remain in `logs/baseline-build.log` and `logs/baseline-flash.log`. Bounded boot evidence is in `logs/baseline-boot-summary.log`; full serial output stays private under `logs/`.

Local tooling checks passed: PowerShell syntax, Python byte compilation, workspace/task JSON parsing, generated board/flash/display/wake symbols, app/assets capacity, and exact partition-table identity with the saved device. The actual flash command also exercised the build wrapper successfully.

User observations: eyes and startup sound work. Chinese wake attempt is inconclusive because the user does not speak Chinese. Check the bundled English wake model next, preserving this initial build first.

Pending first hardware milestone: reliable wake recognition. Microphone RMS/peak/clipping, speaker tone, face-state cycling, Wi-Fi reconnect, and repeatable network-independent wake diagnostics remain Phase 1 work. The original activation and protocol behavior is preserved; this baseline is not the cloud-independent companion yet.

## 2026-11-09 — English wake follow-up

The user cannot reliably test the original Chinese phrase. Selected the existing ESP-SR `wn9_hiesp` model for **“Hi, ESP”**, disabling `wn9_nihaoxiaozhi_tts` in the persistent profile and current generated config. No application or GPIO changes. Initial flashed baseline is preserved at commit `d4c7d63`, tag `step-1-baseline-flashed`, and in the recovery archive.

| Check | Observed result |
|---|---|
| Model selection | PASS: assets parser selects exactly `wn9_hiesp`; Chinese model disabled |
| Build | PASS: app 2,614,000 bytes, 15% app partition free; assets 624,471 bytes |
| Flash | PASS: all images/assets written to COM26 and hashes verified |
| Runtime identity | PASS: app 2.2.2, IDF v5.5.2, ELF SHA prefix `6c5a6adc2`; no OTA replacement observed |
| Runtime model | PASS: `AfeWakeWord: Model 0: wn9_hiesp`; AFE feed/fetch 512; returned to idle |
| Physical wake test | PASS: user confirms “It wakes or responds” when testing “Hi, E-S-P” |
| Serial recognition / interaction | PASS: `Wake word detected: Hi,ESP`; idle -> connecting -> listening -> speaking -> listening |
| Microphone path | PASS for wake recognition; aggregate level/clipping diagnostics remain untested |
| Display / speaker | User previously confirmed eyes and startup cue work |
| Independent/offline diagnostics | Not run; baseline retains original network/cloud behavior |

English build artifacts and resolved config are archived separately in `recovery/2026-11-09-COM26/english-wake/`. App SHA256: `645cb17fe386c7b8fd3d7a020a688ee8e8e4d834824c530e4628ef715bf342fe`. Logs: `logs/english-wake-build.log`, `logs/english-wake-flash.log`, `logs/english-wake-boot-summary.log`, and bounded wake-test capture. Full serial output stays Git-ignored.

**First hardware smoke-test milestone passed:** local source build, safe flash/recovery, serial boot, working eyes/startup sound, and a usable English wake phrase. Stop here for review before adding the network-independent diagnostic mode. Phase 1's mic statistics, local tone/face cycling, offline repeatability, and Wi-Fi outage recovery are still open.

The 60-second user-test capture independently confirms wake recognition and the listening/speaking state cycle. It contains no panic/watchdog event. Lowest reported internal SRAM during that capture: 82,179 bytes; final free SRAM: 86,115 bytes. This is a short functional test, not a long-duration memory test. Firmware/config checkpoint: `abe237d`, tag `step-1-hardware-verified`.
