# Hardware baseline

Last updated: 2026-11-09. Development device: **COM26**.

## Verified by hardware probe

- ESP32-S3 revision 0.2; 40 MHz crystal; USB Serial/JTAG connection.
- Detected flash: **8 MB**, manufacturer `c8`, device `4017`, quad, 3.3 V.
- Chip identification reports embedded **8 MB AP_3v3 PSRAM**. Baseline boot successfully initialized it at 80 MHz and added all 8 MB to the allocator; a separate stress test has not been run.
- The compiled baseline boots on COM26, initializes OLED/LVGL and simplex I2S, connects to Wi-Fi, reaches idle, and starts the AFE task with `wn9_nihaoxiaozhi_tts`. The user confirms working eyes and audible startup sound. Microphone response and phrase detection still need verification. The wiring below remains source evidence, not a pin-by-pin electrical verification.

## Wiring to preserve

Working source: `Source/xiaozhi-esp32-2.2.2/`. Original reference remains under `_Reference_Code/Xiaozhi-for-XiaoESP32S3-master/`.

Authoritative pin definitions: [`board_config.h`](../Source/xiaozhi-esp32-2.2.2/main/boards/seeedstudio-s3-wifi/board_config.h).

| Peripheral | Signal | ESP32-S3 GPIO | XIAO pin |
|---|---|---:|---|
| OLED | SDA | 5 | D4 |
| OLED | SCL | 6 | D5 |
| I2S microphone | BCLK / SCK | 44 | D7 |
| I2S microphone | WS / LRCK | 9 | D10 |
| I2S microphone | SD / DOUT | 1 | D0 |
| MAX98357A amplifier | BCLK | 7 | D8 |
| MAX98357A amplifier | LRC / LRCK | 4 | D3 |
| MAX98357A amplifier | DIN | 2 | D1 |

The reference specifies a 3.3 V I2S MEMS microphone, MAX98357A amplifier with 5 V VIN recommended, and 4 ohm / 3 W speaker. Preserve the existing supply wiring and shared ground. The exact installed microphone model and OLED controller have not been physically confirmed.

Other definitions: onboard LED GPIO21; BOOT GPIO0; volume-up GPIO3; volume-down GPIO8. The touch placeholder also uses GPIO3. The board creates both GPIO3 button objects; touch press/release handlers are active, while BOOT and volume handlers are commented out. Do not change this overlap incidentally.

## Build and driver settings

| Setting | Baseline |
|---|---|
| IDF target | `esp32s3` |
| Board Kconfig symbol | `CONFIG_BOARD_TYPE_SEEEDSTUDIO_S3_WIFI=y` |
| Flash | `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` |
| Partition CSV | `partitions/v2/8m.csv` |
| Display | `CONFIG_OLED_SSD1306_128X64=y` |
| OLED bus | I2C0, address `0x3C`, 400 kHz; mirror X and Y |
| Microphone | I2S1 RX, 16 kHz, mono, left slot |
| Speaker | I2S0 TX, 24 kHz, mono, left slot |
| Audio codec | Existing `NoAudioCodecSimplex`; separate input/output buses |
| Installed toolchain | ESP-IDF v5.5.2 at `C:/esp/v5.5.2/esp-idf` |
| Installed VS Code extension | ESP-IDF 2.3.0 |

The I2S driver uses 32-bit slots. The codec converts microphone input to signed 16-bit PCM with a right shift of 12 and clipping, and scales speaker PCM into 32-bit output. Preserve the proven codec path. Although the board header defines `AUDIO_INPUT_REFERENCE`, the selected codec has one input channel and no reference channel; device AEC is not established by that define.

Source inconsistencies found during inspection:

- Board README names IDF v5.4.1 and also mentions a 5.5 prompt. The actual component manifest requires **IDF >=5.5.2**.
- Reference `config.json` has an incorrect board symbol and malformed 8 MB flash entry. Use the explicit settings above and verify the generated `sdkconfig`.
- Generic S3 defaults select 16 MB flash and generic defaults select the 16 MB partition CSV. Override both for this verified 8 MB device.
- Board README suggests 128x32, but the repository overview and custom animated face target 128x64. The 128x32 UI does not create the eye/mouth objects used by the animation timer.

## Display and wake-word behavior

The existing OLED face updates every 60 ms. `SetFaceState(FaceState::Idle/Listening/Speaking)` selects the expression; `SetEmotion()` is a no-op in this fork. Use the existing face implementation in diagnostics.

S3 defaults select `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y`, suggesting the phrase **“Ni hao Xiao Zhi”**. This remains a build default until the flashed asset model and a detected event confirm it. Runtime wake detection is selected from the models loaded from the **assets partition**, not solely from a Kconfig option. Missing or unflashed assets can leave wake detection unavailable.

Relevant source paths, relative to the firmware root:

- `main/boards/seeedstudio-s3-wifi/seeedstudio_s3_wifi_board.cc`: OLED, controls, codec construction.
- `main/audio/codecs/no_audio_codec.cc`: I2S channels and PCM conversion.
- `main/display/oled_display.cc`: face drawing and animation.
- `main/assets.cc`: speech model loading from local assets.
- `main/audio/audio_service.cc`: microphone task and model-backed wake detector selection.
- `main/audio/wake_words/afe_wake_word.cc`: model metadata and wake event callback.

Wake detection stops on a successful event; an isolated repeatability test must re-arm it. Existing production startup applies assets inside its activation flow, so offline diagnostics must apply local assets explicitly.

## Flash layout

Source: `partitions/v2/8m.csv`. These are the intended build offsets; see the recovery/build documentation for the read-back device layout and flash commands.

| Partition | Offset | Size |
|---|---:|---:|
| NVS | `0x9000` | `0x4000` |
| OTA metadata | `0xD000` | `0x2000` |
| PHY initialization | `0xF000` | `0x1000` |
| OTA0 application | `0x20000` | `0x2F0000` |
| OTA1 application | `0x310000` | `0x2F0000` |
| Assets, including wake model | `0x600000` | `0x200000` |

Bootloader is flashed at `0x0`; the partition table is at `0x8000`. Preserve or regenerate and flash compatible assets alongside the application.

## First hardware smoke-test checklist

Record evidence and firmware/build identity in [`TEST_LOG.md`](TEST_LOG.md). Unchecked items are pending, not failures.

- [x] COM26 chip and physical flash identification.
- [x] Save recovery image and checksum; verify immutable regions before writes.
- [x] Successful build/flash with correct board, flash, partition and display settings.
- [x] Boot log shows PSRAM initialized and reports free internal heap/PSRAM.
- [x] User confirms eyes and audible startup cue work.
- [ ] OLED initializes at 0x3C; idle face and blink are visible; listening/speaking expressions work.
- [ ] Microphone 16 kHz PCM peak/RMS responds to silence, voice and gentle taps; clipping remains controlled.
- [ ] Low-volume local speaker tone/sample is audible without obvious distortion.
- [ ] Wi-Fi connects, reports an IP address and recovers after access point interruption.
- [x] Local asset model name logged; Chinese wake attempt inconclusive for non-Chinese speaker.
- [ ] Repeated wake events appear in serial with Wi-Fi unavailable and no cloud connection.
- [ ] No watchdog reset or persistent memory decline during the smoke test.

Use aggregate microphone levels, not continuous raw audio dumps. Keep one owner of microphone reads: direct codec diagnostics must not race the audio service input task. Physical display/speaker observations require the person at the device; serial initialization alone does not prove those peripherals work.
