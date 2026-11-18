# Companion roadmap progress

Updated 2026-11-18. Current passed checkpoint: **Phase 5a: three consecutive armed physical wake/question/answer/READY cycles**, with correct clear answers confirmed by the user. Saved technical results verify each complete turn and zero input/output clipping. The original working robot and reference repository remain separate.

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
| 4e: manual microphone turn | Passed: real four-second capture → Gemini → correct spoken answer → READY. Exactly 128,000 input bytes sent and 91,200 reply samples played; input/output clipping zero. User confirms the answer. [Question results](ESP32_QUESTIONS.md) |
| 5a: one armed wake/question/reply | Passed: three consecutive new physical wakes, exact input upload, complete reply/playback, normal close, READY and correct clear answers confirmed by the user. Self-paced tester autosaves results. [Wake results](ESP32_WAKE.md) |
| 5b: follow-up conversation | Pending: follow-up listening, quiet timeout, re-wake, interruption and repeated recovery checks. |

The next small gate is **a follow-up question after the robot's answer**, followed by a quiet timeout to idle and a fresh wake. The current firmware still requires a separate arm for each cloud turn. Capture remains shift 15/four seconds; boot is offline/unarmed and ordinary wake remains local outside an armed trial. Both Desktop question testers remain repeatable references with saved evidence.

The first finished companion milestone still requires **wake -> spoken question -> spoken answer -> follow-up -> answer -> idle -> wake again**. The complete manual microphone turn is now established. Extended uptime, live audio streaming, interruption handling and broader network-fault tests remain open.
