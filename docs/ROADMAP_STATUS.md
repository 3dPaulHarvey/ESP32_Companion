# Companion roadmap progress

Updated 2026-11-17. Current checkpoint: **Phase 4c, manual Gemini speech through the development robot's speaker**. The user confirms clear speech, normal speed, speaking animation and READY recovery. The original working robot and reference repository remain separate.

| Roadmap phase | Status and evidence |
|---|---|
| 0: recoverable firmware baseline | Passed: pinned ESP-IDF build, development identity, original flash backup and recovery instructions. [Build/recovery](BUILD_AND_FLASH.md) |
| 1: independent local hardware | Eyes, tones, microphone levels, local English wake and device-side Wi-Fi reconnect verified. Microphone headroom still needs calibration; physical AP toggle was skipped by the user. [Hardware](HARDWARE.md) |
| 2: local companion cycle | Passed: repeatable wake, bounded local capture, cue, cancellation and READY recovery. [Local capture](LOCAL_CAPTURE.md) |
| 3: Worker and desktop Gemini | Passed: hosted health/authentication/shared request limit, temporary-token setup and a correct prerecorded audio question/answer, with clear desktop playback. [Cloud overview](CLOUD_OVERVIEW.md) |
| 4a: ESP32 session request | Passed: private NVS provisioning, HTTPS rejection/success and local work during requests. [Session checkpoint](ESP32_SESSIONS.md) |
| 4b: ESP32 Gemini setup | Passed: direct secure setup acknowledgment and normal closure. [Setup checkpoint](ESP32_GEMINI_SETUP.md) |
| 4c: ESP32 speaker output | Passed: fixed text greeting -> bounded Gemini PCM -> existing speaker -> READY. [Current voice checkpoint](ESP32_VOICE.md) |
| 4: complete manual microphone turn | Next: calibrate/lengthen local microphone capture, then verify a spoken question and correct audible answer from the ESP32. |
| 5: wake and follow-up conversation | Pending: connect cloud audio to wake, follow-up listening, quiet timeout, re-wake, and repeated recovery checks. |

The next small gate is **microphone preparation**, not changing Cloudflare. The current two-second capture is shorter than the successful 2.41-second desktop question fixture. Use quiet/normal-voice measurements to choose input headroom and a bounded capture length, then verify local capture before uploading a question. Keep cloud speech manual until that complete turn is verified.

The first finished companion milestone still requires **wake -> spoken question -> spoken answer -> follow-up -> answer -> idle -> wake again**. Fixed speaker playback establishes one part of that path. Extended uptime, live audio streaming, interruption handling and broader network-fault tests remain open.
