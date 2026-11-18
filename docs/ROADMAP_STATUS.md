# Companion roadmap progress

Updated 2026-11-18. Current checkpoint: **Phase 4d measurements reviewed and applied; Phase 4e manual question test ready for the user**. The original working robot and reference repository remain separate.

| Roadmap phase | Status and evidence |
|---|---|
| 0: recoverable firmware baseline | Passed: pinned ESP-IDF build, development identity, original flash backup and recovery instructions. [Build/recovery](BUILD_AND_FLASH.md) |
| 1: independent local hardware | Eyes, tones, microphone levels, local English wake and device-side Wi-Fi reconnect verified. User microphone measurements reviewed; physical AP toggle was skipped. [Hardware](HARDWARE.md) |
| 2: local companion cycle | Passed: repeatable wake, bounded local capture, cue, cancellation and READY recovery. [Local capture](LOCAL_CAPTURE.md) |
| 3: Worker and desktop Gemini | Passed: hosted health/authentication/shared request limit, temporary-token setup and a correct prerecorded audio question/answer, with clear desktop playback. [Cloud overview](CLOUD_OVERVIEW.md) |
| 4a: ESP32 session request | Passed: private NVS provisioning, HTTPS rejection/success and local work during requests. [Session checkpoint](ESP32_SESSIONS.md) |
| 4b: ESP32 Gemini setup | Passed: direct secure setup acknowledgment and normal closure. [Setup checkpoint](ESP32_GEMINI_SETUP.md) |
| 4c: ESP32 speaker output | Passed: fixed text greeting -> bounded Gemini PCM -> existing speaker -> READY. [Current voice checkpoint](ESP32_VOICE.md) |
| 4d: microphone preparation | Three quiet + three speech samples accepted; zero speech clipping at shift 14, 12.72 dB level contrast, four-second phrase fit confirmed. Capture shift 15 applied for additional headroom; wake remains 12. [Calibration](MICROPHONE_CALIBRATOR.md) |
| 4e: manual microphone turn | Firmware and clickable test prepared; local guards/cancellation and app checks passed. Actual spoken question/correct audible answer pending the user's run. [Question test](ESP32_QUESTIONS.md) |
| 5: wake and follow-up conversation | Pending: connect cloud audio to wake, follow-up listening, quiet timeout, re-wake, and repeated recovery checks. |

The next small gate is **one correct spoken question and answer through the robot**. COM26 now uses calibrated capture shift 15 and a four-second window. The Desktop Robot Spoken Question Test waits for the user's Start, uploads that capture, then saves technical results and a separate physical confirmation. Normal wake and the calibration app remain local. Earlier mistimed two-second observations were superseded by the completed self-paced calibration.

The first finished companion milestone still requires **wake -> spoken question -> spoken answer -> follow-up -> answer -> idle -> wake again**. Fixed speaker playback establishes one part of that path. Extended uptime, live audio streaming, interruption handling and broader network-fault tests remain open.
