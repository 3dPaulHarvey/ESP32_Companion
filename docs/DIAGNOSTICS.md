# Local hardware diagnostics

This compile-time profile runs the fixed XIAO board, I2S codec, OLED face timer, audio service and English AFE wake model locally. It uses the shared companion runtime with cloud conversation features disabled. Wi-Fi is off at boot, and the local model comes directly from the flashed assets partition.

The normal workflow is `-Profile standalone`, which is the wrapper default. The original baseline is no longer an active build profile; its saved English-wake firmware remains under `recovery/2026-11-09-COM26/english-wake/`, separate from the original device's full-flash backup. See [recovery instructions](BUILD_AND_FLASH.md).

## Build and flash

From the repository root:

```powershell
.\tools\idf.ps1 -Action build -Profile diagnostics
.\tools\idf.ps1 -Action flash -Profile diagnostics -Port COM26
.\tools\idf.ps1 -Action monitor -Profile diagnostics -Port COM26
```

The diagnostics use `%LOCALAPPDATA%/ESP32_Companion/build-diagnostics/`, with their generated `sdkconfig` inside that build folder. Defaults load the fixed hardware configuration followed by `config/diagnostics.defaults`. The diagnostic console uses primary USB Serial/JTAG so COM26 accepts commands. Generated output stays outside Dropbox.

Diagnostic startup explicitly installs the USB Serial/JTAG RX/TX driver and binds the console VFS to it before polling nonblocking input. Selecting USB as primary alone did not provide usable input with this installed IDF's default polling backend.

To return to standalone operation, close the monitor and run:

```powershell
.\tools\idf.ps1 -Action flash -Profile standalone -Port COM26
```

## What runs

1. Report physical flash capacity, partition addresses, microphone/output rates and channel counts.
2. Show idle/blink, listening, speaking, and idle faces through the existing OLED implementation.
3. Play a quiet 600 Hz then 900 Hz generated tone at 24 kHz, before starting audio tasks. Saved speaker volume is retained.
4. Apply local assets and verify an AFE wake detector is available.
5. Capture microphone PCM through the existing sole input task. Log sample count, RMS, peak, and clipped samples about once a second; no raw audio leaves the device.
6. On “Hi, E-S-P,” log `WAKE_DETECTED`, show listening/speaking faces, play a local acknowledgment, return to idle and explicitly re-arm the detector. No conversation service is involved.
7. Report internal free/minimum/largest heap block and PSRAM total/free every 10 seconds.

Expected serial markers:

```text
HardwareDiagnostics: MODE local diagnostics; cloud=disabled; WiFi=off
HardwareDiagnostics: AUDIO input=16000Hz/1ch output=24000Hz/1ch reference=0
AfeWakeWord: Model 0: wn9_hiesp
HardwareDiagnostics: WAKE_READY count=0 phrase=Hi,ESP WiFi=off cloud=disabled
MicDiagnostics: Mic level: samples=... rms=... peak=... clipped=...
HardwareDiagnostics: WAKE_DETECTED count=1 WiFi=off cloud=disabled
HardwareDiagnostics: WAKE_READY count=1 WiFi=off cloud=disabled
```

## USB serial menu

Commands are single lowercase characters; Enter is optional.

| Key | Action |
|---|---|
| `h` | Print menu |
| `n` | Run a labelled synthetic local state cycle (no voice recognition implied) |
| `x` | Cancel the current local PCM capture while LISTENING |
| `d` | Repeat idle/listening/speaking/idle display cycle |
| `t` | Play the existing local popup sample through the audio playback pipeline |
| `s` | Report wake count, Wi-Fi status and memory |
| `w` | Start the existing Wi-Fi manager with saved credentials; print IP/RSSI upon connection |
| `o` | Stop Wi-Fi station/radio; local wake remains available |
| `r` | Disconnect a connected station and let the existing manager reconnect automatically |

The wake/cue sequence uses the explicit local controller with [bounded microphone capture](LOCAL_CAPTURE.md). LISTENING waits for two seconds of real PCM; the cue marks the end. Face/cue test commands are ignored during an active cycle.

The Wi-Fi test uses `WifiManager` directly in station mode. It starts no configuration hotspot and does not change saved credentials. A missing access point is retried with bounded scan backoff. The `r` command tests device-side reconnection; physically disabling/re-enabling the access point is a separate observation.

For bounded captures without opening the interactive monitor:

```powershell
python .\tools\capture_serial.py --reset --seconds 30 --name diagnostic-boot
python .\tools\capture_serial.py --send d --seconds 60 --name diagnostic-offline-wake
python .\tools\capture_serial.py --send w --seconds 30 --name diagnostic-wifi
python .\tools\capture_serial.py --send r --seconds 30 --name diagnostic-wifi-retry
python .\tools\capture_serial.py --send o --seconds 20 --name diagnostic-wifi-off
```

Selected hardware/status lines are printed. Full logs remain Git-ignored under `logs/`.

## Physical checks

- Observe all three face states and continuing idle blink.
- Listen for the two rising startup tones and the local acknowledgment; note buzz, clipping, or silence.
- Compare several seconds of room silence, normal speech, and a gentle microphone tap. Speech should raise RMS/peak; a tap should create a peak. Do not impose a universal RMS threshold. A few clipped tap samples differ from persistent clipping during normal voice.
- Say “Hi, E-S-P” at least three times, pausing until `WAKE_READY` / the idle face each time. Confirm increasing counts while `WiFi=off`.
- Start Wi-Fi, verify IP, test disconnect/retry, turn it off, and repeat a wake.
- Reboot and confirm Wi-Fi is off again and local wake still works.

Record observed results and any untested cases in [TEST_LOG.md](TEST_LOG.md). Source inspection and successful driver initialization alone do not pass audible/visible checks.
