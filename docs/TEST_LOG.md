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
