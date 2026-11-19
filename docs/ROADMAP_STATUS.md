# Companion roadmap progress

Updated 2026-11-18. Current passed checkpoint: **Phase 5c standalone functional gate plus bounded upload-write recovery**. Saved real trials and the human unplugged checklist verify autonomous operation. Ten synthetic sessions / twenty answers pass controlled write faults; targeted cancellation/shutdown and link-loss recovery also pass. The earlier failure's exact native cause was not recorded, and broader reliability remains open. The original working robot and reference repository remain separate.

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
| 5c: standalone operation and robustness | Functional gate passed: autonomous two-answer/quiet/READY/fresh-wake trials and human-confirmed cold boot on a separate USB supply. Upload recovery now passes ten controlled synthetic sessions and targeted cancellation/shutdown/link-loss checks. Actual-voice stress, prolonged idle, broader network faults, longer turn-taking and interruption remain open. [Standalone results](ESP32_STANDALONE.md), [upload recovery](UPLOAD_RECOVERY.md) |

The next small gate is **ten actual-voice conversation/idle cycles with explicit failure/recovery accounting**, followed by prolonged idle and broader outage/recovery checks. The send-path investigation is complete within its documented limits: bounded short/temporary-write recovery is corrected and tested, while the exact native cause of the old uninstrumented event cannot be recovered. COM26 runs `standalone` with the upload-recovery update; saved Wi-Fi, model wake, four-second shift-15 capture, two-answer limit and no PC heartbeat remain. Controlled Desktop apps pause autonomy temporarily.

The requested **wake -> spoken question -> spoken answer -> follow-up -> answer -> idle -> wake again** sequence is verified both under the Desktop arm and in standalone operation. The user confirms a separate-USB-supply cold boot without PC control. Reliability, extended uptime, longer/live turn-taking, interruption and broader network-fault tests remain open before the broader companion milestone is complete.
