# Companion roadmap progress

Updated 2026-11-18. Current passed checkpoint: **Phase 5c first functional gate: autonomous wake conversations and separate-USB-supply cold boot/re-wake**. Saved quiet/full trials and the human unplugged checklist support the pass. One recorded upload failure recovered on later fresh wakes; transport reliability remains open. The original working robot and reference repository remain separate.

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
| 5b: bounded follow-up | Passed: quiet control plus two full physical follow-up conversations; same connection/context, five correct clear answers, quiet closure, READY and fresh wake. Eight captures/five playbacks have zero clipping. [Results](ESP32_FOLLOWUP.md) |
| 5c: standalone operation and robustness | First functional gate passed: autonomous wake/two-answer/quiet/READY/fresh-wake trials, plus human-confirmed cold boot on a separate USB supply without PC USB. One saved audio_send_failed attempt recovered on later wakes and remains a reliability issue. Ten conversation/idle cycles, prolonged idle, broader network faults, longer turn-taking and interruption remain open. [Results](ESP32_STANDALONE.md) |

The next small gate is **ten conversation/idle cycles with explicit failure/recovery accounting**, including investigation of the saved upload failure, followed by prolonged idle and broader outage/recovery checks. COM26 runs the verified functional `standalone` image: saved Wi-Fi starts at boot, a physical wake opens the bounded conversation, and no PC heartbeat is needed. Capture stays shift 15/four seconds. The preceding `followup` checkpoint remains an armed reference; controlled Desktop apps pause autonomy temporarily.

The requested **wake -> spoken question -> spoken answer -> follow-up -> answer -> idle -> wake again** sequence is verified both under the Desktop arm and in standalone operation. The user confirms a separate-USB-supply cold boot without PC control. Reliability, extended uptime, longer/live turn-taking, interruption and broader network-fault tests remain open before the broader companion milestone is complete.
