# Companion audio

The fixed XIAO ESP32-S3 has separate I2S buses: a mono 16 kHz microphone
on I2S1 and a mono 24 kHz MAX98357A speaker on I2S0. Both use left slots and
32-bit I2S words. `NoAudioCodecSimplex` owns these channels.

`AudioService::GetInstance()` runs the microphone and speaker tasks. The
microphone task is the only reader and switches between two paths:

- Local AFE wake detection using the bundled English `wn9_hiesp` model.
  Input converts to 16-bit PCM with shift 12. Wake detection keeps no
  rolling recording and sends no wake audio to a server.
- Bounded local capture for calibration or an explicit question. The
  microphone/question profiles use reviewed shift 15, collect headroom
  statistics and transfer retained question PCM directly to the Gemini
  session. Cancelled and local-only recordings are erased.

Gemini replies arrive as 24 kHz PCM and play through the bounded speaker
queue. Playback uses finite I2S writes and trailing silence before READY
so the last samples leave the DMA buffer before wake is rearmed. The
saved output volume and existing half-amplitude speech conversion remain.

The bundled end-of-capture `popup.ogg` cue is the only Opus use: a local
cue decoder converts its 16 kHz mono audio to the 24 kHz speaker rate.
It has no network protocol, encoder, send queue or server AEC timestamps.
`esp_audio_codec` supplies that decoder and `esp_audio_effects` supplies
its rate conversion.

The build retains calibration and synthetic conversation checks for the
same hardware. Other codecs, duplex/PDM configurations, alternative wake
engines, generic voice processors and the raw-audio debug uploader were
removed during platform specialization.
