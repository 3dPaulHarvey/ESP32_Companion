# Hardware baseline

Last updated: 2026-11-18. Development device: **COM26**, MAC **44:bd:8d:ec:38:e0**.

## Verified by hardware probe

- ESP32-S3 revision 0.2; 40 MHz crystal; USB Serial/JTAG connection.
- Detected flash: **8 MB**, manufacturer `c8`, device `4017`, quad, 3.3 V.
- Chip identification reports embedded **8 MB AP_3v3 PSRAM**. Baseline boot successfully initialized it at 80 MHz and added all 8 MB to the allocator; a separate stress test has not been run.
- The compiled baseline boots on COM26, initializes OLED/LVGL and simplex I2S, connects to Wi-Fi, and reaches idle. After switching to bundled `wn9_hiesp`, serial confirms that model and the running AFE task. The user confirms working eyes, startup sound, and an English wake response. The wiring below remains source evidence, not a pin-by-pin electrical verification.
- Local diagnostic firmware now loads the model without cloud startup. User and serial evidence confirm repeated wake/cue/re-arm with Wi-Fi off, face-state cycling, and generated speaker tones. Microphone statistics respond to sound; close/loud input clips and input headroom still needs calibration before voice streaming. Saved-credential Wi-Fi connection and device-side reconnect passed; AP power-off/on was skipped by the user.
- Phase 4a's session profile passes private NVS device-credential provisioning, certificate-verified HTTPS 401/200 checks, credential persistence, and a local capture/cue cycle during a successful request. [Session profile/results](ESP32_SESSIONS.md).
- Phase 4b's `gemini` profile verifies six consecutive direct Gemini setup/close cycles, concurrent local capture and offline recovery. The user confirms eyes/wake/listening/cue after flashing. Lowest observed internal free watermark across those short runs was 53,743 bytes. [Setup profile/results](ESP32_GEMINI_SETUP.md).
- Phase 4c's `voice` profile receives a fixed Gemini greeting and plays 24 kHz PCM through the original I2S speaker task, then restores READY/wake. The user confirms clear speech at normal speed and speaking/READY behavior. Repeated playback, busy guards, device-side Wi-Fi loss and offline recovery pass. [Speaker profile/results](ESP32_VOICE.md).
- Phase 4d's calibrator supplied three accepted quiet and three speech samples, with zero clipping at shift 14 and 12.72 dB level contrast. Capture shift 15 was applied for extra headroom; wake stays at 12. [Calibration procedure/results](MICROPHONE_CALIBRATOR.md).
- Phase 4e's current `question` profile adds explicit USB-started microphone upload and bounded Gemini playback. Local guards/cancellation/recovery and host-app tests pass; the actual spoken-question answer awaits the user. Normal wake/calibration stay local and Wi-Fi is off at boot. [Run the question test](ESP32_QUESTIONS.md).

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

The I2S driver uses 32-bit slots. Normal/wake input converts microphone words to signed 16-bit PCM with a right shift of 12 and clipping; speaker PCM is scaled into 32-bit output. Microphone/question profiles read raw words for bounded capture, using reviewed shift 15 and measuring headroom before clipping. Wake conversion stays unchanged. Although the board header defines `AUDIO_INPUT_REFERENCE`, the selected codec has one input channel and no reference channel; device AEC is not established by that define.

Source inconsistencies found during inspection:

- Board README names IDF v5.4.1 and also mentions a 5.5 prompt. The actual component manifest requires **IDF >=5.5.2**.
- Reference `config.json` has an incorrect board symbol and malformed 8 MB flash entry. Use the explicit settings above and verify the generated `sdkconfig`.
- Generic S3 defaults select 16 MB flash and generic defaults select the 16 MB partition CSV. Override both for this verified 8 MB device.
- Board README suggests 128x32, but the repository overview and custom animated face target 128x64. The 128x32 UI does not create the eye/mouth objects used by the animation timer.

## Display and wake-word behavior

The existing OLED face updates every 60 ms. `SetFaceState(FaceState::Idle/Listening/Speaking)` selects the expression; `SetEmotion()` is a no-op in this fork. Use the existing face implementation in diagnostics.

Phase 2b holds a steady listening pose by suppressing random blinks during Listening and resetting the blink on state changes. It also clamps listening-eye dimensions to positive values. Diagnostic status labels show READY, LISTENING and CUE. This clarification was built/flashed after the user reported that the earlier listening pose did not hold. On 2026-11-17, the user confirmed the recent physical test works well. Audio capture timing passed independently in the earlier serial test; microphone headroom remains uncalibrated.

The first baseline boot loaded `wn9_nihaoxiaozhi_tts` from the generic S3 defaults. The user does not speak Chinese, so its phrase test was inconclusive. The development profile now disables that model and selects bundled English **“Hi, ESP”** (`CONFIG_SR_WN_WN9_HIESP=y`), spoken “Hi, E-S-P.” Runtime wake detection is selected from models loaded from the **assets partition**, not solely from a Kconfig option. Missing or unflashed assets can leave wake detection unavailable; confirm the new model name in serial before testing it.

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
- [x] User confirms wake response to “Hi, E-S-P”; microphone path works for recognition.
- [x] OLED initializes at 0x3C; user confirms idle/listening/speaking expressions work.
- [x] Microphone 16 kHz PCM sample count/RMS/peak/clipping measured during quiet, voice and tap test; wake works from 1–2 feet.
- [x] Review three quiet and three normal-speech captures; apply extra capture headroom while preserving wake gain.
- [ ] Verify one intelligible microphone question and a correct audible Gemini answer at the applied gain.
- [x] User confirms low-volume generated tones and local acknowledgment sample work.
- [x] Wi-Fi connects, obtains IP and recovers after a device-side disconnect.
- [ ] Physical access-point disable/re-enable test (skipped by user).
- [x] Local model `wn9_hiesp` logged and supported English phrase tested; original Chinese baseline preserved.
- [x] Three wake events/cues/re-arms recorded with Wi-Fi off and cloud startup disabled.
- [x] No panic/watchdog in bounded captures; internal minimum heap stabilized at 83,519 bytes during Wi-Fi tests.
- [ ] Extended memory/uptime soak (not part of the short diagnostic run).

Use aggregate microphone levels, not continuous raw audio dumps. Keep one owner of microphone reads: direct codec diagnostics must not race the audio service input task. Physical display/speaker observations require the person at the device; serial initialization alone does not prove those peripherals work.
