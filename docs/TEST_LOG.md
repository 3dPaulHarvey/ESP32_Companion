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

## 2026-11-09 — English wake follow-up

The user cannot reliably test the original Chinese phrase. Selected the existing ESP-SR `wn9_hiesp` model for **“Hi, ESP”**, disabling `wn9_nihaoxiaozhi_tts` in the persistent profile and current generated config. No application or GPIO changes. Initial flashed baseline is preserved at commit `d4c7d63`, tag `step-1-baseline-flashed`, and in the recovery archive.

| Check | Observed result |
|---|---|
| Model selection | PASS: assets parser selects exactly `wn9_hiesp`; Chinese model disabled |
| Build | PASS: app 2,614,000 bytes, 15% app partition free; assets 624,471 bytes |
| Flash | PASS: all images/assets written to COM26 and hashes verified |
| Runtime identity | PASS: app 2.2.2, IDF v5.5.2, ELF SHA prefix `6c5a6adc2`; no OTA replacement observed |
| Runtime model | PASS: `AfeWakeWord: Model 0: wn9_hiesp`; AFE feed/fetch 512; returned to idle |
| Physical wake test | PASS: user confirms “It wakes or responds” when testing “Hi, E-S-P” |
| Serial recognition / interaction | PASS: `Wake word detected: Hi,ESP`; idle -> connecting -> listening -> speaking -> listening |
| Microphone path | PASS for wake recognition; aggregate level/clipping diagnostics remain untested |
| Display / speaker | User previously confirmed eyes and startup cue work |
| Independent/offline diagnostics | Not run; baseline retains original network/cloud behavior |

English build artifacts and resolved config are archived separately in `recovery/2026-11-09-COM26/english-wake/`. App SHA256: `645cb17fe386c7b8fd3d7a020a688ee8e8e4d834824c530e4628ef715bf342fe`. Logs: `logs/english-wake-build.log`, `logs/english-wake-flash.log`, `logs/english-wake-boot-summary.log`, and bounded wake-test capture. Full serial output stays Git-ignored.

**First hardware smoke-test milestone passed:** local source build, safe flash/recovery, serial boot, working eyes/startup sound, and a usable English wake phrase. Stop here for review before adding the network-independent diagnostic mode. Phase 1's mic statistics, local tone/face cycling, offline repeatability, and Wi-Fi outage recovery are still open.

The 60-second user-test capture independently confirms wake recognition and the listening/speaking state cycle. It contains no panic/watchdog event. Lowest reported internal SRAM during that capture: 82,179 bytes; final free SRAM: 86,115 bytes. This is a short functional test, not a long-duration memory test. Firmware/config checkpoint: `abe237d`, tag `step-1-hardware-verified`.

## 2026-11-09 — Phase 1 local diagnostics

A compile-time startup profile uses the production board/codec/display and app-owned audio service, loads wake models directly from local assets, and leaves application/cloud startup uncalled. The radio remains off until the USB serial Wi-Fi command. Added aggregate microphone statistics to the existing wake input task and a local wake acknowledgment/re-arm loop. Baseline and diagnostic builds/configurations remain separate.

| Check | Observed result |
|---|---|
| Diagnostic build/flash | PASS; corrected USB-input version is 2,536,144 bytes, 18% app-partition headroom; writes/hash checks passed |
| Baseline regression build | PASS; normal firmware is 2,614,000 bytes; diagnostic source excluded from its compilation and normal startup/console selections retained |
| Offline startup | PASS: explicit cloud-disabled/Wi-Fi-off banner, local `wn9_hiesp` model loaded; no activation/OTA/protocol startup observed |
| Flash/partition/rates | PASS: runtime 8,388,608-byte flash; expected six partitions; 16 kHz mono input / 24 kHz mono output, no physical AEC reference |
| Display and tone | PASS: user confirms tones, expressions, and repeated wake work |
| Offline repeatability | PASS: serial counts 1, 2, 3, each `WiFi=off cloud=disabled`, local cue and `WAKE_READY` re-arm; no cloud call required |
| Microphone measurement | PASS instrumentation; 49 windows in physical capture, RMS 5,738–32,184; 29 clipped windows / 159,318 clipped samples during close/loud speech or background audio, as identified by user |
| Normal-distance level check | PASS wake at 1–2 feet, confirmed by user; lower-level windows had zero clipping, but higher-level speech still clipped. Input headroom is not calibrated; existing driver conversion preserved |
| Offline heap | After warm-up, internal free roughly 100 KB; lowest reported 97,843 bytes; PSRAM free roughly 7.94 MB. Short capture only, no long soak claim |
| USB commands | PASS after explicitly installing USB RX/TX driver and VFS binding; `w` and `r` processed successfully |
| Wi-Fi connection | PASS using saved credentials, IP obtained, RSSI approximately -49 dBm; no cloud startup |
| Device-side reconnection | PASS: intentional disconnect at uptime 74,649 ms; connected/IP at 77,029 ms (about 2.38 seconds); microphone statistics continued |
| Access-point outage | Skipped at user's request; actual AP disable/re-enable behavior remains unverified |
| Wi-Fi-off | PASS: `o` completed its event-loop shutdown, radio/connection stopped, microphone statistics continued, no panic |
| Reboot | PASS: local model/AFE reinitialized, idle face restored; USB `s` returned `WiFi=off connected=0 cloud=disabled` |

Code review identified a pre-existing Wi-Fi manager/event-loop lock inversion risk during station stop. The diagnostic `o` command dispatches shutdown on the default ESP event loop and waits for atomic completion before reporting off; no managed component was edited.

See [DIAGNOSTICS.md](DIAGNOSTICS.md) for exact commands, expected serial markers and the test sequence. Original full-flash and both baseline build variants remain preserved. Physical test capture: `logs/diagnostics-offline-physical-summary.log`. Full logs remain private/Git-ignored.

Additional physical result: user confirms normal-voice wake from 1–2 feet. The corrected USB build recorded three local wake/cue/re-arm cycles with Wi-Fi on, independently of the earlier three offline cycles. The normal-distance/retry capture contains low-level RMS windows around 3,161–7,727 with no clipping and higher-level windows up to RMS 27,999 with clipping. Because the capture is not acoustically calibrated and includes speech/cues, do not assign a universal noise floor or claim all normal speech is unclipped. Check microphone PCM headroom before Gemini streaming; this phase adds observation without changing the known-good sample conversion.

Final diagnostic artifacts: `recovery/2026-11-09-COM26/phase-1-diagnostics-final/`; app SHA256 `b20d176652abf5bb2074c82e1d0d5f2762302ee91399b7f35e77684c1bbcd2b0`. Implementation commit: `8d826cd`. Both build profiles passed; diagnostics has 18% app-partition headroom, baseline 15%. The baseline regression build emitted existing OLED unused-variable / vendor ioctl macro warnings; no build errors.

Corrected USB build Wi-Fi captures maintained at least 83,519 bytes of internal heap and an 81,920-byte largest internal block during connection, retry and shutdown. A roughly one-minute idle capture showed stable minimum heap rather than continuing decline. This does not replace a long soak test.

Scope reached: independent hardware diagnostic mode, local model loading/repeated wake, display/tone/PCM statistics, Wi-Fi connection and device-side recovery. Remaining observations: physical AP outage (user skipped), extended soak, and microphone headroom calibration. Cloudflare/Gemini and production companion state-machine integration remain later phases.

Final state: COM26 is left in the local diagnostic profile, awake-ready/idle with Wi-Fi off. Final capture: `logs/diagnostics-final-reboot-summary.log`; runtime ELF prefix `4ba178bcf`, app v2.2.2 / IDF v5.5.2. No panic/assert/watchdog observed. Serial capture has closed. Restore normal cloud-backed baseline with the command in `DIAGNOSTICS.md` when desired.

## 2026-11-09 — Phase 2a: explicit local states

Following the user's request to verify small steps, this increment adds the explicit `IDLE -> WAKE_DETECTED -> LISTENING -> SPEAKING -> IDLE` controller to the existing cloud-disabled profile. It is a **timed UI/cue rehearsal**, with `capture=disabled`; the 400 ms LISTENING state does not yet capture a PCM segment. Bounded capture remains the next implementation task.

The controller owns only state/deadlines/counters. The diagnostic main task owns eye changes, cue playback and wake re-arming. Busy wakes and `d`/`t` overrides are rejected. Serial (`n`) triggers and genuine voice wakes have separate counters/source labels.

| Check | Result |
|---|---|
| Target identity | COM26 initially absent; user reconnected it; USB identity and esptool MAC match the original development device. COM24 was a different device and was not flashed |
| Host controller tests | PASS with C++17 and `-Wall -Wextra -Werror -pedantic`: boundaries, busy duplicates, delayed ticks, ten cycles, reset and state names |
| Cross-compiler invariants | PASS: constexpr `static_assert` timing / repeated-cycle checks compiled by ESP32 toolchain |
| Build | PASS: app 2,537,536 bytes, 18% app-partition headroom; partition table unchanged |
| Flash | PASS: normal diagnostics-profile flash; all image hash checks passed |
| Boot | PASS: ELF prefix `2570c1b20`, explicit IDLE / timed-ui-rehearsal / capture-disabled banner, zero counters, English model ready, Wi-Fi off |
| Automated serial cycles | PASS: ten complete synthetic cycles, ordered states and dwell times, busy duplicate wake and `d`/`t` rejection; final IDLE, started=completed=10, voice=0, serial=10 |
| First physical voice run | User reported misses/sticking in background noise. Trace recorded ten accepted voice cycles, all four transitions and ten completions, no rejected voice event or panic; it does not establish how many spoken attempts were missed |
| Quieter physical retry | PASS: user confirms all ten attempts worked at 1–2 feet with background audio paused; trace contains 11 complete voice-triggered cycles (IDs 21–31), each with all four transitions and re-arm |
| Network interruption | PASS: one synthetic cycle completed while the station was intentionally disconnected, then Wi-Fi reconnected/obtained IP; ordered recovery and final connected status verified |
| Wi-Fi shutdown | PASS: `o` completed shutdown and local microphone sampling continued |
| Reboot | PASS: local IDLE, started/completed/voice/serial counters all zero, English wake model ready, Wi-Fi off and cloud disabled |

Build/flash logs: `logs/phase2a-build.log`, `logs/phase2a-flash.log`. New images/config/flash map archived separately at `recovery/2026-11-09-COM26/phase-2a-local-states/`; app SHA256 `3bbb1b5143b61aa506487b90f003699c2019dce57f66f4e2d3059eed3121598b`. Previous Phase 1 image and original full-flash recovery remain saved. See [LOCAL_STATES.md](LOCAL_STATES.md) for the verification procedure.

Implementation checkpoint: `84c658c`. Serial verification logs are under `logs/phase2a-serial-cycles-private.log`, `phase2a-real-wakes-summary.log`, `phase2a-real-wakes-quiet-summary.log`, and `phase2a-network-cycle-private.log`. Synthetic and real-wake counts are not interchangeable: the verification observed 10 synthetic cycles initially, 10 accepted voice cycles in the first run, 11 voice cycles in the quieter retry, and one additional synthetic network-interruption cycle.

The first noisy trial remains recorded as a user-reported failure; ten accepted detections completing normally do not prove every spoken attempt was recognized or every cue was heard. The quiet retry supplied a physical pass. Background-noise robustness and microphone headroom calibration remain open. No microphone segment is recorded by the LISTENING state in this increment; it remains a timed UI rehearsal before bounded PCM capture is added.

Before the final reboot, status confirmed `IDLE started=32 completed=32 voice_started=21 serial_started=11`. All captured cycles completed, and no panic/assert/watchdog appeared. Lowest reported internal heap across these captures was 71,055 bytes during the Wi-Fi test; this is a bounded functional check, not a long soak. Final capture `logs/phase2a-final-reboot-summary.log` confirms the device is left idle on COM26 with Wi-Fi off and serial capture closed. Existing local editor settings were preserved outside the implementation commits.

**Phase 2a state/UI checkpoint verified.** Phase 2 remains incomplete until bounded PCM capture (and its level/headroom checks) is implemented and verified. Original baseline and prior diagnostic recovery images remain available.

## Phase 2b: bounded PCM capture (implemented 2026-11-09; checkpoint recorded 2026-11-10)

LISTENING now captures a real, bounded 16 kHz mono int16 segment in the existing sole microphone input task. It discards 3,200 initial samples and retains exactly 32,000 samples (64,000 bytes, two seconds) in PSRAM. The worker wipes/frees that buffer before publishing measurements. No raw audio file, replay, transcription or cloud request is produced. Success drives SPEAKING/end cue; cancellation or failure aborts without claiming a completed cycle. Worker/controller watchdogs and request IDs bound recovery and reject stale results.

| Check | Result |
|---|---|
| Target | COM26 USB identity matched development board `44:BD:8D:EC:38:E0` before flash; no other board flashed |
| Portable tests | PASS: strict C++17 controller tests and PCM buffer tests, including exact counts, discard, short chunks, extrema, cancellation/watchdog boundaries and retry |
| Builds | PASS: diagnostics and baseline, including the later display correction; corrected diagnostic app 2,541,488 bytes (`0x26c7b0`), 17% app-partition headroom; baseline 2,614,032 bytes (`0x27e310`), 15% headroom |
| Flash | PASS: initial and display-corrected diagnostic images written and hashes verified on COM26 |
| Initial boot | PASS: bounded-pcm-capture banner, English wake model, idle, Wi-Fi off, cloud disabled, zero counters/pending |
| Cancellation/recovery | PASS: request 1 cancelled, aborted=1, cleanup acknowledged and wake re-armed; no false success |
| Automated captures | PASS: requests 2–4 each 32,000 samples / 64,000 bytes, elapsed 2,129–2,132 ms including discard; final IDLE started=4 completed=3 aborted=1 pending=0; busy-command guards passed |
| Voice-triggered capture | Serial PASS: three genuine wake requests 5–7, each exact sample count, elapsed 2,120–2,127 ms, success/cue/idle/re-arm; completed=6, voice_started=3, serial_started=4, aborted=1 |
| Physical listening display | User reported that listening expression did not hold. Shared random blink and a negative eye-height frame were found; fixed by holding Listening steady, resetting blinks, clamping dimensions and adding READY/LISTENING/CUE status. Correction built/flashed, but **physical retest pending** |
| Microphone quality | Voice captures RMS 1,362 / 6,634 / 6,991, peaks 5,020 / 32,767 / 32,767, clipped samples 0 / 92 / 117. Counts prove capture, not intelligibility; noisy environment and existing headroom remain unresolved. Conversion/gain unchanged |
| User deferral | User requested stopping physical testing due to background noise and testing/adjusting the mic later. No further spoken test was requested |
| Current availability | COM26 absent at the 2026-11-10 status check; no post-correction boot/physical observation obtained |
| Cloud | Overview prepared only. No Worker deployment, Gemini key, temporary token or AI conversation is implemented |

Evidence remains under ignored `logs/`: `phase2b-build.log`, `phase2b-baseline-build.log`, `phase2b-flash.log`, `phase2b-boot-summary.log`, `phase2b-cancel-recovery-private.log`, `phase2b-spoken-captures-summary.log`, and `phase2b-face-{build,baseline-build,flash}.log`. Existing vendor macro/unused OLED variable warnings remain; builds succeeded. No panic/assert was observed in the bounded capture tests. Phase 2a network recovery is not a substitute for a new capture-under-network-interruption test; that additional check was not run.

Initial Phase 2b artifacts remain in `recovery/2026-11-09-COM26/phase-2b-local-capture/`; the later display-corrected images/config/manifest are saved separately in `phase-2b-local-capture-display/`. Original full-flash backup and prior checkpoints remain preserved. See [LOCAL_CAPTURE.md](LOCAL_CAPTURE.md) for expected interaction and [CLOUD_OVERVIEW.md](CLOUD_OVERVIEW.md) for the subsequent cloud stage.

**Implemented, with physical retest pending.** On the next quiet-room test, confirm LISTENING and steady eyes for the capture window, CUE/end sound, then READY/blinking eyes. Test and calibrate microphone headroom before voice streaming. Do not mark the complete Phase 2 exit gate or cloud conversation as passed yet.

## 2026-11-17 — Physical follow-up

The user reports: “the recent irl test is working well” and requests proceeding with development. Record the corrected listening/status/cue/idle interaction as a user-confirmed physical pass. This supersedes the physical-retest deferral above; no fresh serial log, exact cycle count, acoustic measurement or new firmware flash accompanies this report.

The existing automated capture/cancellation results and three voice-triggered sample-count results remain the measured evidence. Microphone headroom calibration, the capture-under-network-interruption check and the full repeated-capture exit sequence remain open. Proceed with the independent local Worker health-endpoint preparation; this does not establish a Gemini connection or close those hardware checks.

## 2026-11-17 — Phase 3a: local Worker health endpoint

Created a separate `cloudflare-worker/` project with an ES module request handler, Wrangler configuration, pinned development dependencies/lockfile and local runtime tests. This starts cloud-service development independently of the remaining microphone quality work. Firmware source and device flash were not changed in this checkpoint.

| Check | Observed result |
|---|---|
| Node setup | Node v24.22.3 / npm 11.19.1 prepared under ignored `.tools/`; official Node download SHA256 verified and recorded in `.tools/node-download-manifest.json` |
| Pinned tools | Wrangler 4.164.0, matching Miniflare 5.20261117.0-beta, workerd 1.20261117.1; `npm ci` succeeds with the saved lockfile |
| Runtime tests | PASS: four groups in real local workerd — GET/JSON/cache headers, HEAD with no body, four disallowed methods with 405/Allow, eight unknown path/method combinations with 404 |
| Packaging | PASS: Wrangler `deploy --dry-run`, no bindings, bundle approximately 0.81 KiB / gzip 0.43 KiB; no deployment performed |
| Actual development server | PASS: `wrangler dev --local` started on `127.0.0.1:8787`; TCP listener confirmed on loopback |
| HTTP probe | PASS: GET `/health` 200 and expected JSON, HEAD 200/empty, POST `/health` 405/Allow, GET `/device/session` 404; no-store headers verified |
| Process cleanup | PASS: exited the Wrangler menu with `x`; development server shut down |
| Platform | Windows 10 Pro 10.0.19045. Pinned checks pass here, although current Cloudflare docs support Windows 11 rather than Windows 10 |
| Scope | No Cloudflare account login, remote resource creation, provider secrets, session endpoint, Gemini requests or ESP32 cloud integration |

The first test attempt found that the installed Miniflare 5 API has a different constructor shape from the generic online example; tests now use its shipped worker/config/manifest API. The PowerShell wrapper also preserves native exit codes while allowing warnings on stderr. Both runtime tests and the dry run passed after those corrections. These were development-tool issues, not robot failures.

Logs: `logs/worker-health-install.log`, `worker-health-tests.log`, `worker-health-bundle.log`, `worker-health-http.log`. The automatic tests dispose their runtime; the development server is stopped after the manual HTTP probe. No Gemini key is needed for the current code. Existing user editor settings remain untouched and unstaged.

**Local health checkpoint passed.** Next: account setup and hosted HTTPS health verification, then device authentication/request limits and Gemini token support. The endpoint reports that this Worker is alive; it does not report end-to-end robot or Gemini readiness. Commands and platform notes are in [CLOUD_WORKER.md](CLOUD_WORKER.md).

## 2026-11-17 — Hosted starter created through dashboard

The user is registered/logged into Cloudflare and followed the dashboard flow to deploy the Hello World starter as `esp32-companion-control`. The supplied editor screenshot shows the starter active, and the user confirms its Visit link works. An independent HTTPS GET to `https://esp32-companion-control.stiener.workers.dev/` returned `200`, `text/plain;charset=UTF-8`, and `Hello World!`. Evidence: `logs/worker-hosted-starter-http.log`.

Computer/browser control was unavailable, so creation was performed by the user with screen-by-screen guidance. No agent UI mutation or Wrangler account authorization was performed. The next manual step is replacing dashboard `worker.js` with the already-tested local `cloudflare-worker/src/index.js`, deploying, and verifying `/health` plus the hosted method/error behavior. **Hosted starter passed; hosted health checkpoint remains pending.** No Gemini key or session endpoint is configured.

## 2026-11-17 — Hosted health checkpoint verified

The user copied the prepared handler into the dashboard editor, deployed it, and reports it is working correctly. Independent HTTPS requests to `https://esp32-companion-control.stiener.workers.dev` verified the current hosted behavior:

| Request | Result |
|---|---|
| `GET /health` | PASS: 200, JSON content type, exact status/service/api_version payload |
| `HEAD /health` | PASS: 200 with empty body |
| `POST /health` | PASS: 405, `Allow: GET, HEAD`, method-not-allowed JSON |
| `GET /` | PASS: 404 with not-found JSON |
| `POST /device/session` | PASS: 404; session issuance is still unimplemented |
| Cache control | PASS: all five responses use `Cache-Control: no-store` |

Requests used normal certificate-validated HTTPS and carried no credentials or personal audio. Full public response evidence is saved in ignored `logs/worker-hosted-health-http.log`. Firmware was not changed. **Hosted health checkpoint passed.** Next baby step: create one device-authentication Worker secret and prove correct/missing/wrong credential behavior before adding Gemini. No Gemini key, device credential, authentication endpoint or request limiter has been created at this checkpoint.

## 2026-11-17 — Device credential prepared locally

The user reached the Worker's Production runtime variables/secrets settings; the supplied screenshot shows no variables yet. Generated `DEVICE_AUTH_TOKEN` with Python `secrets.token_hex(32)` in `cloudflare-worker/.dev.vars`. The exclusive-create write preserved any existing file, and Git exclusion was verified before writing. The generated value is 64 lowercase hex characters (256 random bits) and was not printed or included in tracked files.

Only the local credential exists at this checkpoint. Instructions supplied for adding it as a Production Worker **Secret** through Add variable. Hosted save is pending user confirmation. No remote request, firmware change, authentication handler, request limiter, or Gemini setup was performed in this increment. The deployed health handler remains the last verified service behavior.

## 2026-11-17 — Phase 3b: local device authentication verified

The first generated device token appeared in automatically attached IDE-selected text. Replaced its local value with fresh randomness without printing the replacement. The user then confirmed saving the new `DEVICE_AUTH_TOKEN` as a Production Secret. Hosted correctness/matching of that value still requires the authenticated test after code deployment.

Added `POST /device/check` using the server secret and an Authorization Bearer header. Configured secrets must be 64 lowercase hex characters; missing/malformed configuration rejects access with 503. Missing/incorrect/malformed supplied credentials get the same 401 JSON/challenge. Valid fixed-length credentials use Cloudflare `crypto.subtle.timingSafeEqual` and return only `{"authenticated":true}`. Credentials in a query or body are ignored. `/health` remains public, and `/device/session` remains 404.

| Check | Result |
|---|---|
| Runtime suite | PASS: nine groups in local workerd, retaining all four health-route groups plus correct/invalid/query-body/configuration/method auth groups |
| Auth rejection cases | PASS: missing, wrong first/last character, short/long, wrong scheme, duplicate-style header, case-changed value and invalid text all return 401 |
| Configuration failure | PASS: absent secret or a value including surrounding quotes returns 503; health remains 200 |
| Packaging | PASS: Wrangler dry run, 2.07 KiB / gzip 0.88 KiB; no publishing performed |
| Real local HTTP | PASS: Wrangler loaded `.dev.vars` with its value hidden; desktop checker observed missing=401, wrong=401, correct=200 |
| Desktop client | Python compilation passes; an isolated fixture test verified rejection of unsafe target forms and refusal of redirects, using no real credential |
| Credential output | Token not printed in runtime logs, test results or desktop checker output; local secret file remains Git-ignored |
| Cleanup | Local development server shut down with `x` |
| Hosted verification | Pending user deployment of the new handler; only hosted health was previously verified |

`tools/check_device_auth.py` reads the ignored credential, validates the target origin (HTTPS except loopback HTTP), rejects redirects and emits only controlled status metadata. It verifies exact JSON/header contracts without printing arbitrary response bodies. Logs: `logs/worker-auth-runtime-tests.log`, `worker-auth-bundle.log`, `worker-auth-local-http.log`, and the latest `worker-device-auth-check.log`.

**Local authentication checkpoint passed; hosted pass not yet claimed.** No Gemini request, request limiter, device provisioning or firmware change is included. Manual deployment and verification instructions are in [DEVICE_AUTH.md](DEVICE_AUTH.md).

## 2026-11-17 — First hosted authentication run: credential mismatch suspected

After the user reported deploying the new handler, the desktop checker observed missing credential=401 (PASS), incorrect credential=401 (PASS), and current local credential=401 (FAIL; expected 200). The first two cases passed exact response/challenge checks. The local token file still has the expected 64-character format. Because this handler would return 503 for absent/malformed server configuration, the observed 401 suggests that the saved Production value differs from the current local value; the earlier rotation is one possible cause. This is an inference, not a readback of Cloudflare's secret.

Follow-up public checks verified `GET /health`=200 with its exact payload/no-store header and `GET /device/check`=405 with `Allow: POST`. The hosted handler is active and health remains functional. No token value was printed or logged. Evidence: `logs/worker-auth-hosted-mismatch.log`. The older `worker-device-auth-check.log` still records the successful local run, because the checker only writes that file after a complete pass.

**Hosted authentication remains unverified.** Next action: edit the existing Production `DEVICE_AUTH_TOKEN` Secret to match the current local private file, deploy that change, then rerun the three-case checker. No firmware or authentication-code change is indicated by this result, and no Gemini call was made.

## 2026-11-17 — Hosted device authentication verified

The user replaced the existing Production Secret with the current local value and deployed that change. The unchanged desktop checker then passed all three cases against `https://esp32-companion-control.stiener.workers.dev`:

| Case | Result |
|---|---|
| Missing credential | PASS: 401 with exact unauthorized response/challenge |
| Incorrect credential | PASS: 401 with exact unauthorized response/challenge |
| Correct current local credential | PASS: 200 with exact `{"authenticated":true}` |
| Response headers | PASS: JSON content type and no-store checked for each auth response |
| Public health follow-up | PASS: GET `/health`=200 with exact payload and no-store, using the usual Companion client identifier |

The initial mismatch was resolved by synchronizing the secret; no token regeneration or code change was needed. No credential value was printed/logged, and no audio or Gemini request was sent. Archived hosted results: `logs/worker-auth-hosted-verified.log`; health evidence: `logs/worker-auth-hosted-health-followup.log`. The previous mismatch record remains preserved separately.

One auxiliary health probe using the default Python user agent received HTTP 403. A follow-up using the usual explicit Companion client identifier returned the expected 200 response. No Worker or security settings were changed; the cause of that single 403 was not independently established. The three authentication requests all used the existing Companion checker and passed.

**Hosted authentication checkpoint passed.** This verifies the desktop-to-Worker credential path, not ESP32 transport or Gemini conversation. Next incremental work: request limits before provider calls, then supported Gemini model/quota/token verification. Firmware remains at its earlier local diagnostic checkpoint, and microphone headroom calibration remains open before streaming.

## 2026-11-17 — Phase 3c: local request limit and recovery verified

Added `DEVICE_RATE_LIMITER` with a threshold of 10 authenticated control requests/60 seconds, namespace `20261117`. Authentication happens first; a fixed nonsecret key represents the one development device. Throttling returns 429/rate-limited JSON with Retry-After 60 and no-store. Missing/invalid limiter configuration and limiter errors reject authenticated requests with 503. Health and invalid-credential responses remain independent of the device quota. No provider call or firmware change is included.

| Check | Result |
|---|---|
| Runtime suite | PASS: 11 groups, retaining all health/auth tests plus missing/invalid limiter and quota behavior |
| Runtime quota | PASS: wrong credentials do not spend quota; 10 admitted requests then 429; varying query does not reset allowance; health remains 200 and unauthenticated requests remain 401 |
| Packaging | PASS: 3.08 KiB / gzip 1.12 KiB; dry run lists DEVICE_RATE_LIMITER at 10 requests/60s |
| Real local burst | PASS: 10 authenticated requests admitted, next request 429 with Retry-After 60; health and wrong/missing auth checks passed during throttling |
| Recovery | PASS: authenticated request returns 200 after a 61-second retry wait |
| Post-recovery auth regression | PASS: missing=401, wrong=401, correct=200 using the real local credential |
| Tooling | Python byte compilation, PowerShell syntax and whitespace checks pass; wrapper adds login/whoami/deploy commands and checks the deployment target name |
| Cleanup | Local development server shut down with x after HTTP checks |
| Hosted state | Last verified live version remains Phase 3b; limiter is not deployed yet |

Logs: `logs/worker-rate-runtime-tests.log`, `worker-rate-bundle.log`, `worker-rate-local-verified.log`, `worker-rate-auth-regression.log`, and latest `worker-rate-limit-check.log`. Secret values remain absent from emitted results. Required-secret names (not values) are declared in configuration; observability remains enabled to match the supplied dashboard state.

Cloudflare's current docs say rate-limit bindings are not visible in the dashboard, so this increment needs Wrangler configuration deployment. CLI account status was checked privately and is not authenticated. A reduced-scope OAuth login is prepared (`account:read`, `user:read`, `workers_scripts:write`) with OS-keyring storage requested. Authorization and hosted deployment have not yet occurred. See [RATE_LIMITS.md](RATE_LIMITS.md).

**Local request-limit checkpoint passed; hosted checkpoint pending.** Cloudflare enforcement is approximate per location and cannot guarantee a strict worldwide quota or billing cap. Provider quota checks remain required before Gemini calls. No paid plan change or new Gemini access has been configured.

## 2026-11-17 — Hosted rate-limit investigation and final shared-counter verification

The user completed reduced-scope Wrangler OAuth authorization. Login and a private whoami check succeeded; account/user read and Worker script management are available, with OS-keyring storage requested and no fallback warning observed. The existing Worker's deployment history was retrieved before publishing. No account credential value was printed or placed in project source.

Deployment preflight showed that the local default would enable preview URLs while the remote setting was false. Declined that upload and the subsequent configuration-replacement prompt, explicitly set `preview_urls: false`, and repeated the packaging check. Enabled observability was preserved. Generated root `.wrangler/` account-cache metadata is now ignored by Git.

The initial native-binding deployment (`74d26067-9ef9-4552-8c55-10b49a430362`) confirmed DEVICE_RATE_LIMITER at 10 requests/60 seconds, but a bounded 35-request hosted burst produced no 429. A timed follow-up admitted 35 requests in 7.61 seconds plus probes at about 18, 28 and 38 seconds across ATL/LAX locations. Metadata confirmed the binding and the current deployment. A second revision (`5e0495ce-1ec3-4edf-b830-48bfbffe0f42`) added an explicit delta of 1 and a response revision marker; 35 requests still passed without 429, with that revision confirmed. The underlying native-API behavior was not independently diagnosed. These runs were not accepted as a hosted limiter pass. Evidence: `logs/worker-rate-hosted-timing-diagnostic.log`, `worker-rate-limit-check.log`, and private version/deployment metadata.

The final implementation follows Cloudflare's documented strict-limit approach: one globally addressed SQLite-backed Durable Object per development device. A synchronous transaction deletes expired admissions, counts remaining rows and admits only when fewer than 10 timestamps remain in the rolling 60-second window. At most 10 timestamps are retained; no secret or audio is stored. The counter key is fixed and nonsecret. Authentication precedes the counter; unavailable configuration/backend rejects authenticated traffic with 503.

| Final check | Observed result |
|---|---|
| Runtime tests | PASS: 12 groups in local workerd, covering health/auth/misconfiguration/quota and concurrent admission |
| Concurrent budget | PASS: 20 simultaneous authenticated requests produce exactly 10 accepted and 10 throttled responses |
| Final packaging | PASS: 4.38 KiB / gzip 1.61 KiB; SQLite Durable Object binding recognized |
| Hosted deployment | PASS: existing URL retained; version `b3740e90-919a-44ab-a803-86472f19231a`; startup 5 ms; revision marker `rate-limit-v3` |
| Hosted burst | PASS: 10 admitted requests, next request 429 with a remaining retry delay (initially 58 seconds) |
| Hosted rejection/health during throttle | PASS: missing/wrong credentials remain 401, health remains 200, changed query remains throttled |
| Hosted recovery | PASS: correct authenticated request returns 200 after waiting the advertised remaining delay plus one second |
| Local HTTP burst/recovery | PASS with the same final SQLite implementation: 10 admissions, 429, health/auth unaffected, recovery successful |
| Authentication regression | PASS after recovery on both targets: missing=401, wrong=401, correct=200 |
| Credential handling | Private value remains Git-ignored and absent from source/result output; Production secret remained functional through deployment |
| Cleanup | Final local development server shut down with x; hosted service remains running |

Final evidence: `logs/worker-rate-v3-runtime-tests.log`, `worker-rate-v3-bundle.log`, `worker-rate-v3-hosted-verified.log`, `worker-rate-v3-local-verified.log`, `worker-rate-v3-hosted-auth-regression.log`, `worker-rate-v3-local-auth-regression.log`, and current `worker-rate-deployment.json`. Archived auth results were checked against their respective target origins. Both checker tools now allow distinct log names to avoid concurrent tests overwriting one another. The final HTTP checker permits at most 11 burst requests and rejects a serving-revision mismatch.

**Phase 3c request limiting verified locally and online.** SQLite-backed Durable Objects are supported on Workers Free subject to its usage limits; no plan upgrade was performed. This admission guard is not a total spending cap, and Gemini/provider quota verification remains open. No Gemini key, token minting, personal voice upload, ESP32 provisioning or firmware change occurred. See [RATE_LIMITS.md](RATE_LIMITS.md) for the final configuration, commands and limitations; earlier native-binding entries remain historical evidence.

## 2026-11-17 - Phase 3d: hosted Gemini token and connection verified

The user created a Gemini API key in Google AI Studio, reported that the project shows Free tier, and saved a Production Worker binding named `GEMINI_API_KEY`. Active-version metadata showed that it was `plain_text`. The ordinary Secret PUT refused a type conflict (Cloudflare code 10053); no key value was printed. A version-based Secret patch converted the binding to `secret_text`, verified that type and deployed version `1b228ab7-bb81-48ae-9044-c71c20c8ed17`. The value passed privately between subprocesses in memory/stdin, never through command arguments or a local credential file. An exact-value audit of 139 existing Wrangler/project log files found zero matches; no log redaction was needed. Earlier Cloudflare versions remain in its version history.

Checked current official Google model/pricing/token/WebSocket documentation and the live v1beta discovery schema. Gemini 3.8 Live is listed with free-tier input/output; free-tier data may be used to improve Google's products. The tutorial's token-lock example differs from the live REST schema. Implemented the schema's `bidiGenerateContentSetup` with no field mask, so the entire setup is server-owned. The hosted connection subsequently accepted that request. Project access is verified at this checkpoint; remaining daily/concurrent quota is not measured.

`POST /device/session` authenticates the existing device credential, shares the 10/60-second transactional admission counter, and obtains a one-use temporary token. New sessions must start within 60 seconds; expiration is five minutes from issuance. The complete setup locks `models/gemini-3.8-live`, audio output, at most 256 output tokens, a short English companion instruction and output transcription. No tools or session resumption are enabled. Client body/query settings are not parsed or forwarded. The permanent key goes only in a header to the fixed Google HTTPS endpoint; redirects are not followed. Provider responses are bounded to 16 KiB, timeout is 10 seconds, tokens are bounded/validated, and only approved response fields are returned. Provider errors are replaced with controlled error codes and an HTTP-status-only diagnostic header.

Local iterations caught a rejection of empty HTTP POST streams and an unsupported redirect mode before publishing. The final handler ignores client session options and uses Workers' supported manual redirect mode, rejecting redirect responses without following them. Final results:

| Check | Observed result |
|---|---|
| Runtime suite | PASS: all 20 workerd groups; existing health/auth/quota and concurrent-admission tests retained |
| Session gating/policy | PASS: method/auth/key/limiter checks, fixed full REST setup, one-use/expiry request settings and approved response fields |
| Shared budget | PASS: alternating device checks/session calls share ten admissions; excess calls never reach the fake provider |
| Provider failures | PASS: rejection/quota, malformed/oversized/credential-echoing responses, no cross-origin redirect, controlled errors |
| Timeout | PASS: unresponsive fixture provider returns 504 after approximately 10 seconds |
| Packaging | PASS: 8.16 KiB / gzip 2.86 KiB; existing Durable Object binding recognized |
| Hosted deployment | PASS: version `b9a750ce-b8db-464f-8d84-fa4b3b1fe901`, startup 4 ms, revision `gemini-session-v1` |
| Hosted session methods/auth | PASS: GET=405, missing=401, incorrect=401 |
| Real token issuance | PASS: authenticated request=200, exact model/response fields and expected expiry window |
| Real Gemini connection | PASS: temporary token accepted by v1beta constrained WebSocket; `setupComplete` received; connection then closed |
| Auth regression | PASS: hosted device check remains missing=401, incorrect=401, correct=200 |
| Health regression | PASS: hosted exact public payload/no-store/current revision, HTTP 200 |
| Real local HTTP | PASS: health=200, missing session auth=401, valid session auth without a local Gemini key=503; expected missing-key warning; local server then closed with x |
| Credential/audio handling | No token/key printed or logged; permanent key absent from local source/credential files; no user question or microphone audio sent |

`tools/check_gemini_session.mjs` reads only the existing ignored device credential. It validates the service origin, refuses HTTP redirects and emits controlled metadata. Its optional connection uses the temporary token in Google's required in-memory `access_token` WebSocket query parameter, without printing that URL, provider messages, error stacks or close reason. `tools/worker.ps1 -Action session-check` runs the complete issuance/setup check. Both required Production bindings are Secrets; observability and disabled preview URLs are preserved. No new dependency or paid-plan upgrade was introduced.

Evidence: `logs/worker-gemini-runtime-tests.log`, `worker-gemini-bundle.log`, `worker-gemini-session-check.log`, `worker-gemini-hosted-auth-regression.log`, `worker-gemini-local-http.log` and sanitized `worker-gemini-deployment.json`. The rate-checker's expected serving revision now follows `gemini-session-v1`; its auth-only burst still makes no Gemini calls. Final metadata confirms both credential bindings remain `secret_text`.

**Phase 3d token/connection checkpoint passed.** This establishes desktop-to-Worker-to-Gemini session access. Spoken output, audio input, microphone headroom and ESP32 cloud transport remain later checks. Robot firmware, COM26 flash, the original COM24 board and the reference repository were not changed. See [GEMINI_SESSIONS.md](GEMINI_SESSIONS.md).

## 2026-11-17 - First desktop spoken reply verified

After saving Phase 3d at `8796881` / `phase-3d-hosted-gemini-session-verified`, extended the desktop checker with `--speak` and the wrapper's `voice-check` action. The Worker and hosted deployment are unchanged. After a temporary-token setup acknowledgment, the client sends one fixed text greeting request, collects the model's audio until `turnComplete`, and closes. It opens no microphone and sends no personal audio. Response handling serializes async Blob decoding to preserve PCM chunk order, rejects unexpected formats/tools/interruption, caps PCM at 20 seconds and the complete turn at 30 seconds, and writes a standard WAV under ignored logs.

| Check | Observed result |
|---|---|
| Real token/setup | PASS: method/auth/issuance checks and constrained WebSocket setup |
| Real spoken response | PASS: nonempty completed PCM turn received from the fixed text request |
| Independent WAV validation | PASS: Python wave reader, 24,000 Hz / mono / 16-bit PCM, 65,760 frames, 2.74 seconds |
| Sample levels | Peak magnitude 25,382; RMS 4,694.11; zero clipped samples |
| Windows playback | PASS: SoundPlayer Load/PlaySync completed successfully |
| User verification | PASS: user reports clear speech at normal speed |
| Setup-only regression | PASS: original session-check command still receives setupComplete and closes without sending a question/audio |
| Tooling | Node syntax, PowerShell parsing and whitespace checks pass |
| Secrets | Permanent key stays in the Production Secret; temporary credential remains in memory and is absent from WAV/metadata output |

Evidence: `logs/worker-gemini-voice-check.log`, `worker-gemini-voice-check.wav`, `worker-gemini-voice-wave-validation.log`, `worker-gemini-voice-listener-check.log` and the latest `worker-gemini-session-check.log`. All 20 existing Worker runtime groups passed in Phase 3d; no Worker source/config changed in this output increment. No additional runtime dependencies or cloud deployment were required.

**First desktop spoken-output checkpoint passed, including listener confirmation.** This is not yet an audio-input or robot conversation test. Next verify a fixed prerecorded 16 kHz question / 24 kHz response, then integrate ESP32 transport and microphone headroom. See [DESKTOP_VOICE.md](DESKTOP_VOICE.md).

## 2026-11-17 - Phase 3f: prerecorded audio question and answer verified

Generated the fixed synthetic question "What is two plus two?" locally through Windows System.Speech, without opening a microphone. The complete recording plus 80 ms of leading / 320 ms of trailing silence is 2.41 seconds of signed 16-bit little-endian mono PCM at 16 kHz. The generator saves a replay WAV, raw PCM and question/source/format/hash manifest under ignored logs. The desktop checker validates this identity before requesting a token. No text copy of the question is sent to Gemini.

After the existing constrained setup acknowledgment, the new audio-question mode sends raw PCM in paced 40 ms frames, followed by `audioStreamEnd` for the default automatic activity detector. It receives bounded 24 kHz PCM until turn completion and collects only a bounded output transcript for the fixed question's answer check. The client stops on closure/backpressure and retains its six-second input, 20-second output and 30-second total-turn bounds. Setup-only and text-greeting modes remain available. Worker source/configuration, dependency versions and the hosted deployment are unchanged.

| Check | Observed result |
|---|---|
| Offline desktop tests | PASS: six tests, public fixtures/fake socket, no network or real credential |
| Sender contract | PASS: exact PCM bytes reconstructed from frames; 1,280-byte maximum; matching pacing; stream-end after input; no text fallback |
| Failure handling | PASS: excessive buffering prevents sending, mid-input close stops later frames/EOF, empty audio response fails |
| Prior modes | PASS offline: setup-only sends one setup; text greeting still sends one user turn and receives PCM |
| Hosted session/auth | PASS: GET=405, missing/wrong=401, token=200, constrained setup accepted |
| Real audio question | PASS: entire 77,120-byte input sent as 61 audio chunks and stream-end; no question text sent |
| Correct answer | PASS: output transcript says "Two plus two equals four! Is there anything else I can help you with?" |
| Input WAV / samples | PASS: 38,560 frames at 16 kHz, 2.410 s, peak 15,095, RMS 1,181.28, zero clipped samples |
| Output WAV / samples | PASS: 102,720 frames at 24 kHz, 4.280 s, peak 22,845, RMS 3,316.68, zero clipped samples |
| Windows playback | PASS: completed recorded-question and reply playback |
| User verification | PASS: user confirms the correct answer and clear speech at normal speed |
| Credentials / firmware | No permanent or temporary token in audio/transcript/metadata output; no microphone opened, robot flash or reference change |

Evidence: `logs/gemini-input-question.json`, `worker-gemini-audio-check.log`, `worker-gemini-audio-check.transcript.txt`, `worker-gemini-audio-wave-validation.log`, `worker-gemini-audio-listener-check.log` and `worker-gemini-audio-desktop-tests.log`. Repeat commands are documented in [DESKTOP_AUDIO.md](DESKTOP_AUDIO.md). The earlier 20 Worker tests were not repeated because Worker code/configuration did not change; this increment's six desktop tests cover its new sender and existing client modes.

**Desktop prerecorded audio exchange passed, including listener confirmation.** This does not verify the robot's microphone: the fixture is 2.41 seconds, longer than its current two-second local capture. Next prove authenticated Worker/session access from the development ESP32, then add embedded Gemini transport and check microphone headroom/capture duration before live conversation.

## 2026-11-17 - Phase 4a: ESP32 authenticated session requests verified

COM26 was present and independently identified as the original development ESP32-S3, MAC `44:bd:8d:ec:38:e0`. COM23 was also enumerated but never opened or flashed. Saved a new full-flash pre-session backup under `recovery/2026-11-17-phase-4a/`, verified 8,388,608 bytes / SHA256 `b9402e8fd52be5176e8cd421837098fc1d8e4f5eee6807b86ef933f1b7e68f9a`. The original recovery image was not overwritten.

Added a separate `session` profile, output directory/generated configuration, and `COMPANION_SESSION_PROBE` opt-in. It retains local diagnostics, Wi-Fi-off boot and local wake behavior. A private non-echoed USB frame provisions the existing device credential into NVS only after the host verifies the profile and board MAC. Input frames enforce exact lowercase-hex length, bounded time, invalid-frame draining and clearing. The permanent Gemini key stays in Cloudflare and is never provisioned to the ESP32. The application binary was scanned for absence of the real device credential; the flash map excludes NVS. The wrapper verifies COM26/MAC before session-profile flashing.

Manual serial g/b/u requests run in a separate task. They use the fixed Worker HTTPS endpoint, certificate bundle, hostname checking and certificate-date checking. The prior diagnostic configuration had date checking disabled, so the new profile explicitly enables it. Compile guards reject insecure TLS, disabled certificate/date support and maximum DEBUG/VERBOSE logging. SNTP is bounded to 15 seconds when needed; the HTTP phase has its own 15-second deadline. Redirects are disabled. The client drains SDK-cached response data, enforces the 16 KiB body bound, validates exact headers/schema/model/endpoint/expiry, and clears owned credential/Authorization/response buffers before task exit. Temporary credentials are inspected only by a flat borrowed-view parser, then discarded. No Gemini WebSocket or audio upload is implemented in this increment.

The first build exposed an ESP-IDF component-discovery issue: configuration-dependent requirements omitted esp_http_client's header/Kconfig during the initial requirements pass. Declaring the component requirements unconditionally resolved it; the session sources remain profile-gated. All 23 constexpr policy/framing assertions then compiled. The first flashed hardware run correctly rejected Wi-Fi-off access but its new `%lld` timing field was incompatible with the existing nano formatter, so the host checker timed out. A metadata-only follow-up identified the nonnumeric elapsed field. Switched the bounded millisecond log to `%lu`, rebuilt and reflashed; the full hardware check then passed. No provider call or credential provisioning occurred in that first aborted run.

| Final check | Observed result |
|---|---|
| Policy assertions | PASS: 23 compile-time assertions for credential format, dates, schema/expiry, duplicate/oversize rejection and provisioning frame completion/timeout/clearing |
| Host checker unit tests | PASS: six offline tests for board identity, metadata, split credential-echo suppression, fault/output bounds and invalid response metadata |
| Build/flash | PASS: final application 2,563,424 bytes (17% app-partition space free); guarded COM26 flash verified hashes and reset |
| TLS configuration | PASS: CA bundle/name/date checking enabled; insecure TLS and excessive logging refused by compile guards |
| Offline guard | PASS before and after network tests: local HTTP-0/wifi-required result, no outbound session request |
| Provisioning | PASS: partial frame rejected; correct private frame saved; configured state persists after reset |
| Wi-Fi | PASS: connects with existing saved credentials; device-side shutdown succeeds |
| Wrong credential | PASS: 401 exact rejection/challenge, 3,340 ms; duplicate command rejected as busy |
| Missing credential | PASS: 401 exact rejection/challenge, 3,617 ms |
| Correct credential | PASS twice: 200 exact session contract, 3,953 ms and 3,364 ms; token contents never printed/stored |
| Local work during HTTPS | PASS: 32,000-sample/64,000-byte local capture and cue/idle recovery during the first 200 request |
| Local work after shutdown | PASS: offline capture/cue/idle recovery |
| Quiet reboot | PASS: credential remains configured; Wi-Fi off and no automatic session request |
| Physical follow-up | PASS: user confirms eyes, wake, listening and cue all work |
| Memory observations | Internal minimum 67,391 bytes; active-Wi-Fi snapshots 87,651 / 86,103 / 89,243 bytes free; no panic detected by the checker |
| Earlier diagnostics build | PASS after the shared CMake change; not flashed over the verified session profile |

Final application SHA256: `acfd56d8a4c69081a98876be4127efcb1a3feb18699ce937ecc01a21d14f7d8c`. Configuration/app metadata: `logs/esp32-session-build-metadata.json`. Other evidence: `esp32-session-build-final.log`, `esp32-session-flash-final.log`, `esp32-session-hardware-verified.log`, `esp32-session-host-unit-tests.json`, `esp32-session-physical-followup.json` and `esp32-session-diagnostics-regression-build.log`. Host logs contain only controlled statuses/timings/heap metadata; raw serial output is not saved. Production Worker source/deployment and dependency lock are unchanged.

**Phase 4a control-request checkpoint passed on the ESP32.** The temporary response is discarded rather than used to connect to Gemini. Forced device 429/recovery, extended memory soak, direct WebSocket setup and live audio remain later checks. The existing shared backend limit was already verified in Phase 3c. Development credential storage uses ordinary NVS; this profile does not add encryption at rest. Microphone headroom and capture duration remain open before voice streaming. See [ESP32_SESSIONS.md](ESP32_SESSIONS.md).

## 2026-11-17 - Phase 4b: direct ESP32 Gemini setup and closure verified

Saved the Phase 4a checkpoint at `14d8567` / `phase-4a-esp32-session-verified` and worked on `work/phase-4b-esp32-gemini-setup`. COM26 again matched development MAC `44:bd:8d:ec:38:e0`; no other board was opened or flashed. Before changing its firmware, saved `recovery/2026-11-17-phase-4b/before-gemini-full-flash.bin`, verified as 8,388,608 bytes / SHA256 `9550a5988dee3a54b64dd0ffaf9175632af4f3e711cb4e4e67f3f9ef84d38e0e`. This private backup includes the provisioned NVS credential. The original November 9 backup was not overwritten.

Added an opt-in `gemini` profile with its own build/configuration output and the existing COM26/MAC flash guard. Serial `j` obtains a fresh temporary credential through the verified Worker contract, releases the Worker HTTP/TLS client, opens the fixed Google constrained WebSocket, sends the fixed model setup, waits for `setupComplete`, sends normal Close and requires its acknowledgment. The response buffer owns the borrowed temporary credential until transport cleanup. No question, microphone samples, tools or session-resumption messages are sent. Wake remains local, Wi-Fi starts off, and no automatic retry is introduced.

Used the pinned ESP-IDF WebSocket/TLS transports without adding a dependency. CA/hostname/date verification remains enabled, only HTTP 101 is accepted, redirects are not followed, and reserved characters in the token query are percent-encoded. The SDK masks outgoing buffers in place, so all passed payloads use writable storage. Bounded frame tracking handles partial reads, fragmentation and interleaved control frames; messages are limited to 16 KiB and controls to 125 bytes. Owned private buffers are cleared and both transports destroyed before a success result is emitted. SDK-owned copies are normally freed rather than promised to be forensically erased.

The local SDK audit found an ERROR log that can print the complete credential-bearing upgrade request. In this profile CMake compiles logging out of all five `tcp_transport` units; a runtime tag suppression provides a second guard. Compile commands and the application image were checked to confirm that suppression and absence of the unsafe request-dump strings. The real device credential is also absent from the image. The host rejects sensitive transport markers and device-fault output without saving raw serial data. The permanent Gemini API key remains only in the existing Worker Secret.

Initial real connections reached HTTP 101 and a 26-byte setup acknowledgment, but the checker treated an additional data message arriving after Close as an invalid setup response. Added bounded draining of in-flight data after our Close, without accepting it as a close acknowledgment. The first repeated run then completed two cycles and failed a third; that early checker did not retain the failing stage metadata, so its exact cause is unclassified. Improved the checker to preserve controlled failure metadata. A complete run and a separate short repeat then passed **six consecutive** direct cycles; the intermittent failure did not recur. Extended stability/network-fault testing remains open.

| Final check | Observed result |
|---|---|
| Build/flash | PASS: 2,563,024-byte application, 17% app-partition space free; guarded flash hashes verified |
| Compile-time policy | PASS: 24 session/provisioning assertions plus 16 URI/setup/frame assertions |
| Offline host tests | PASS: seven session identity/privacy/fault/bounds tests and six direct-result gate tests |
| Pre-flash security | PASS: secure TLS/date/name configuration, five transport units with logging compiled out, unsafe request logs and real device credential absent from image |
| Wi-Fi-off guard | PASS before and after online checks: local HTTP 0 / wifi_required, no connection attempt |
| Authentication regression | PASS: wrong credential 401 in 2,971 ms; missing credential 401 in 2,446 ms |
| Session-only regression | PASS: valid 200 and exact session contract in 2,750 ms |
| Direct upgrade/setup/close | PASS six times: HTTP 101, setup acknowledgment 26 bytes, close sent/acknowledged, server close code 1000 |
| Direct transport times | 2,237 / 1,927 / 2,240 / 1,955 / 1,915 / 1,925 ms, excluding Worker request time |
| Concurrent local work | PASS: one exact 32,000-sample/64,000-byte local capture/cue/idle cycle while connecting; duplicate j command rejected as busy |
| Offline recovery | PASS: Wi-Fi shutdown followed by local capture/cue/idle recovery |
| Quiet reboot | PASS: Wi-Fi off, credential retained, no automatic cloud request |
| Physical follow-up | PASS: user reports eyes, wake, listening and cue all work after the final reboot |
| Memory observations | Lowest internal free watermark 53,743 bytes; active-Wi-Fi snapshots 74,971-86,639 bytes free; no detected panic in these short runs |
| Earlier profile | PASS: session-only regression build; not flashed over Gemini |
| Cloud/dependencies | Worker source/deployment and dependency versions unchanged; no repeat of the unchanged 20 Worker test groups needed |

Final application SHA256: `892a7c5ebd62dc581374e007a424727aaa7881e4528edeaa11ac4a30ebf4405a`. Evidence: ignored `logs/esp32-gemini-setup-build-close-drain.log`, `esp32-gemini-setup-flash-close-drain.log`, `esp32-gemini-setup-preflash.json`, `esp32-gemini-setup-hardware-verified.log`, `esp32-gemini-setup-short-repeat.log`, `esp32-gemini-setup-host-tests.json`, `esp32-gemini-setup-physical-followup.json`, `esp32-gemini-session-regression-build.log` and the controlled initial `esp32-gemini-setup-diagnostic.json`. No raw provider message, credential-bearing URL or serial stream is retained in those result logs.

**Phase 4b setup-only checkpoint passed, including physical follow-up.** The robot still performs its local LISTENING/capture/CUE/READY sequence and gives no spoken cloud answer. Next add bounded playback of a fixed Gemini reply through its speaker. Microphone headroom, the two-second capture duration, live audio transport and extended fault/uptime checks remain open. See [ESP32_GEMINI_SETUP.md](ESP32_GEMINI_SETUP.md).

## 2026-11-17 - Phase 4c: fixed Gemini speech through the robot verified

Started from `2b34468` / `phase-4b-esp32-gemini-setup-verified` on branch `work/phase-4c-esp32-spoken-output`. COM26 matched the development ESP32-S3 MAC `44:bd:8d:ec:38:e0`. Before flashing, saved and verified the 8,388,608-byte full image `recovery/2026-11-17-phase-4c/before-voice-full-flash.bin`, SHA256 `8f042c9c2ae50ae9ee02a316cc63336db8d34fb9a10b82a732f1580c7a055d34`. It remains private because it includes NVS. The original November 9 backup is unchanged; no other board or reference code was modified.

Added the isolated `voice` build/profile and manual serial `v` command. Existing local wake/listen/capture/cue behavior remains separate, with Wi-Fi off at boot. The job reuses the authenticated temporary session and TLS/WebSocket setup, sends the same fixed English greeting request as the desktop prototype, receives a bounded complete PCM turn and requires a normal close acknowledgment. It then plays through the existing AudioOutputTask, which remains the sole speaker writer. The display loop owns CONNECTING/SPEAKING/READY transitions and wake rearming. Concurrent local captures/faces/cues and duplicate cloud requests are rejected during the manual job. No microphone data is handed to the transport.

Implemented a borrowed-token JSON parser with 64 KiB / 192-token / eight-level limits and strict duplicate/escaped-field-name handling. Canonical base64 decoding requires the exact 24 kHz PCM MIME type, even byte counts and a maximum ten-second/480,000-byte reply. `generationComplete` cannot substitute for `turnComplete`, and empty completed audio, interruption, errors/tools and unexpected messages fail closed. Response traffic is capped at 256 messages / 2 MiB. The reply and JSON workspace live in PSRAM; owned reply/transport allocations are zeroized on release.

The audio adapter reserves the existing output queue, permits at most two 20 ms chunks plus the current write, checks in-flight playback/decode activity, and uses finite 250 ms I2S writes with actual sample counts. It preserves the original slot conversion and saved volume while applying half amplitude for this first speech test. An 80 ms silence tail drains the 60 ms speaker DMA; a further 150 ms settling interval precedes wake rearming. Failed/timeout writes release queued data and return a controlled failure rather than treating a partial playback as success. Normal baseline codec behavior remains behind the disabled feature flag.

The first full build caught a misleading-indentation warning promoted to an error in the new JSON number parser; formatting was corrected without relaxing compiler checks. The first real voice request reached setup/prompt but stopped at `unexpected_server_message` before audio. A desktop diagnostic, logging only whitelisted message-type names/counts, identified a `sessionResumptionUpdate` notice even though the client did not request resumption. Added bounded ignore handling for that status object without retaining a handle or sending resumption messages, plus targeted policy assertions. No raw message or handle was logged.

After that fix, the robot received and played 99,360 bytes / 49,680 samples (2.070 s; playback/settling 2.238 s), but the host checker timed out because it expected local busy-command logs under the wrong tag. Corrected the tag to LocalCompanion and added an offline serial-contract test. This was a checker failure: the controlled device result had verified setup, turn, normal close and complete playback. The user missed that first audible test, so a separate replay was announced and performed. The user then confirmed **clear speech at normal speed, speaking expression and READY**.

| Final check | Observed result |
|---|---|
| Build/flash | PASS: 2,572,480-byte app, 16% app-partition space free; guarded COM26 flash hashes verified |
| Compile-time policy | PASS: 72 assertions, including 32 new nested-JSON/base64/PCM/completion/bounds cases |
| Offline host tests | PASS: seven session + six setup + nine voice tests, including exact sample/timing/result gates and the complete serial guard/release trace |
| Secure configuration | PASS: CA/name/date verification, five tcp_transport units with logging compiled out, unsafe upgrade-request strings and real device credential absent from image, NVS excluded from flash map |
| Listener replay | PASS: 91,680 bytes / 45,840 samples / 1.910 s; playback/settling 2.079 s; full request 9.868 s |
| Repeat 1 | PASS: 82,080 bytes / 41,040 samples / 1.710 s; playback/settling 1.879 s; full request 10.078 s |
| Repeat 2 | PASS: 127,682 bytes / 63,841 samples / about 2.660 s; playback/settling 2.839 s; full request 12.250 s |
| Audio/completion gates | All three final checked greetings have HTTP 200/101, setup/prompt/turn completion, close code 1000, exact played counts, READY/wake recovery and zero clipped source samples |
| Physical confirmation | PASS: user confirms clear speech at normal speed and speaking/READY transition on the announced replay |
| Busy guards | PASS: duplicate v and j, synthetic local capture, face cycle and local cue commands cannot compete with the manual job |
| Device-side Wi-Fi loss | PASS: stopping Wi-Fi during direct connect produces expected websocket_upgrade_failed, zero played samples and READY/wake recovery; complete request 2.896 s; no automatic retry |
| Offline/reboot recovery | PASS: Wi-Fi-off voice rejection, exact local 32,000-sample capture/cue/idle cycle, credential retained, quiet Wi-Fi-off reboot |
| Existing embedded regression | PASS: 401 wrong/missing credential, 200 session-only request, three setup-only closes, concurrent local capture/busy guard, offline recovery and quiet reboot on the voice firmware |
| Earlier-profile build | PASS: Gemini profile rebuilt with voice disabled; not flashed over the current voice profile |
| Memory observations | Lowest internal free watermark 62,431 bytes; active-Wi-Fi snapshots across the final playback/regression records 83,555-85,599 bytes free; no detected panic in these bounded runs |
| Cloud/dependencies | Worker code/deployment and dependency versions unchanged; permanent Gemini key remains server-side |

Application SHA256: `86bc5d34032b3bae0f13819888441fd0f833af5b6d88fde98a01de46dbb6340f`. Final evidence: ignored `logs/esp32-voice-preflash.json`, `esp32-voice-build-metadata-fix.log`, `esp32-voice-flash-metadata-fix.log`, `esp32-voice-listener-replay.log`, `esp32-voice-final-hardware-verified.log`, `esp32-voice-existing-gemini-regression.log`, `esp32-voice-gemini-regression-final.log`, `esp32-voice-host-tests.json` and `esp32-voice-physical-followup.json`. Earlier controlled failure evidence remains in `esp32-voice-first-hardware-check.log` and `esp32-voice-hardware-verified.log`; only bounded metadata is retained. The desktop shape diagnostic stores only whitelisted type names/counts, never provider values or PCM.

**Phase 4c manual speaker checkpoint passed.** This proves fixed-text-to-Gemini-to-robot speech, not microphone-to-cloud conversation. Whole-turn buffering is deliberate for this first output test; a streaming ring buffer, extended audio/network stability, live microphone upload and follow-up sessions remain later work. The next small gate is microphone headroom and a capture window longer than the current two seconds before one manual spoken question/answer. See [ESP32_VOICE.md](ESP32_VOICE.md) and [ROADMAP_STATUS.md](ROADMAP_STATUS.md).

## 2026-11-18 — Phase 4d: self-paced microphone calibrator ready

The user reported missed speaking windows during the short live tests and requested an independent clickable calibrator with autosave, to run without coordinating with chat. This increment delivers that Windows app, a Desktop shortcut, the root `Open Microphone Calibrator.cmd` launcher, and [user instructions](MICROPHONE_CALIBRATOR.md). **Human microphone calibration has not passed yet; it awaits the user's saved samples.**

Earlier two-second baseline attempts are retained privately: quiet RMS 2,557/2,999/949 with no clipped samples; speech-window RMS 2,917/2,926/9,245, with 778 of 32,000 samples clipped in the third attempt. The user missed some windows, so these are uncontrolled observations rather than a verified normal-speech calibration. They motivate more capture headroom and a self-paced test, not a final gain decision.

The new opt-in `microphone` profile captures four seconds / 64,000 samples / 128,000 bytes. Capture reads raw 32-bit I2S words and applies provisional shift 14; the proven wake conversion remains shift 12. Numeric summaries compare both conversions on the same input before clipping and include four one-second windows, raw peak, DC/AC levels and near-full-scale raw counts. The existing input task remains the sole reader. PCM and temporary raw chunks are wiped/freed before metadata publication. Worker/controller limits become 5.5/6 seconds in this profile; older profiles retain two-second behavior.

The app uses manual starts, a countdown, firmware-synchronized green recording signals, Keep/Redo review, cancel, partial-session resume and automatic local saves. A single-instance lock prevents competing session writers. A serial-command allowlist excludes provisioning/cloud commands. The app temporarily holds wake detection between samples and refreshes a ten-second firmware lease; normal close or lease expiry restores local wake. Demo and UI-test results are isolated from actual user calibration.

The first app integration test exposed a test-side race: it checked the saved close status as soon as the USB worker exited, before Tk processed the queued close event. The test now waits for the saved partial state. A later layout check caught an unmapped review control during the speech step. The review panel now replaces the recording controls rather than adding height below them; the final full workflow passes. Computer Use window inspection was attempted twice, but its native pipe was unavailable (`The system cannot find the file specified`, OS error 2). No visual inspection was claimed; Tk layout bounds and the real event loop were checked directly by the app integration test.

| Final check | Observed result |
|---|---|
| Recovery | PASS: private pre-change 8 MB backup verified before writing; original backup retained |
| Build/flash | PASS: microphone app 2,575,840 bytes, about 16% app-partition space free; development MAC guarded; all write hashes verified |
| Compile-time policy | PASS: 91 assertions (24 session, 16 setup, 32 output, 19 microphone/lease) |
| Host tests | PASS: 45 tests (11 session, six setup, nine output, seven microphone metadata, 12 persistence/ownership) |
| App integration | PASS: explicit starts, countdown/recording indication, Keep/Redo, six kept demo samples, optional skip, visible controls and close during capture |
| Offline hardware capture | PASS: firmware RECORDING event, exactly 64,000 samples / 128,000 bytes, four ordered windows and local cue/idle completion; unspecified room input, not a kept user calibration sample |
| Control ownership | PASS: heartbeats retain paused wake beyond ten seconds; mid-capture cancel clears ownership before another trial |
| Close/disconnect | PASS: normal close releases COM26 and rearms wake; simulated abrupt disconnect expires the lease and returns to idle with wake armed and Wi-Fi off |
| Privacy/configuration | PASS: CA/name/date verification, five transport units compiled with logging off, unsafe upgrade strings and actual device credential absent from application; NVS excluded from normal write map |
| Earlier-profile regression | PASS: `voice` profile rebuilt with microphone feature disabled; not flashed over current microphone build |
| Cloud | Worker deployment/key configuration unchanged; calibration opens no cloud session and uploads no microphone audio |
| Human calibration | PENDING: user will run three quiet and three normal-speech samples in the standalone app; optional wake check separately reported |

Application SHA256: `52629fe3f3c900a377e17843065ef819f3bc6865877e5f3308a4723619573ee2`. Private recovery image: `recovery/2026-11-18-phase-4d/before-microphone-full-flash.bin`, 8,388,608 bytes, SHA256 `0d48e0017f2ea47382e71c0c95465593a4bf132480bff4c9a1994b6faa21686f`. Tested write files, generated configuration and manifest are archived under that directory's `built-microphone/` folder.

Final private evidence: `logs/esp32-microphone-preflash.json`, `esp32-microphone-calibrator-build.log`, `esp32-microphone-calibrator-flash.log`, `esp32-calibrator-hardware-qa.json`, `esp32-microphone-host-tests.json`, `calibrator-ui-test.log` and `esp32-microphone-voice-regression-build.log`. User evidence will appear under `logs/microphone-calibration/`; the review tool currently reports no completed user calibration. Next: review those saved measurements before a controlled microphone-to-Gemini question/answer.

## 2026-11-18 — Calibration incorporated; manual question test ready

The user finished and closed the standalone calibrator, then requested that its data be applied and development continue while they were away from the robot. Reviewed the actual complete session, not demo/QA data: `2026-11-18_12-20-12_18333abe`, closed at 20:22:04 UTC. All six attempts were explicitly kept, with three speech-window-fit confirmations. The optional wake check was skipped. Original JSON SHA256: `488860de012e4eedd394ceaa223824c2cc23f58bc6368ccb1be87694c404636f`; the source file remains unchanged at its measured shift 14.

| Accepted speech sample, shift 14 | Overall RMS | Peak | Clipped | Same-input shift-12 clipping-limit count |
|---|---:|---:|---:|---:|
| 1 | 2,434 | 17,049 | 0 | 745 |
| 2 | 1,868 | 15,231 | 0 | 394 |
| 3 | 2,665 | 15,576 | 0 | 928 |

Median quiet-window RMS was 747 and median active speech-window RMS was 3,231: **12.72 dB level contrast**, not a laboratory SNR. No near-full-scale raw words were counted. The maximum speech peak was 52% of int16 full scale, slightly above the chosen half-scale headroom target. Applied capture shift **15** in Kconfig/defaults and the generated builds, retaining **four seconds** and wake shift **12**. The expected largest peak at 15 is at most approximately **8,525**, about 11.7 dB below full scale. That is a prediction from the recorded raw peak; human speech at the new gain is checked by the next question test. No new filter/noise gate was added. Applied decision metadata is separate in ignored `logs/microphone-calibration-applied.json`.

Added an opt-in `question` profile. Explicit serial q initiates one calibrated capture through the existing local controller. Only that capture can retain its PCM allocation for direct transfer from the audio worker to the session task; ordinary wake and calibration still wipe locally. The local end cue precedes the cloud request. The established Worker/TLS path then sends 100 little-endian PCM/base64 frames at 16 kHz, paced at 40 ms, followed by EOF while processing incoming messages. Input is wiped after EOF or on failure. Existing strict bounded reply validation, closure, output-task playback and READY/wake recovery remain in force. Cancellation, duplicate/local/cloud guards, and pre-upload network loss clear retained input and do not retry.

Added a separate Desktop **Robot Spoken Question Test** and root launcher. The user connects, starts each question, follows firmware-synchronized SPEAK NOW, hears the answer and explicitly confirms clear/correct speech and READY. All available numeric capture and output data, errors/cancellation and human feedback autosave under `logs/robot-question-tests/`. Input measurements save before cloud completion, so a later cloud failure does not lose microphone-level evidence. No PCM, transcript, provider message or credential-bearing URL is saved. The original calibrator remains local-only and its UI regression passes.

| Verification | Result |
|---|---|
| Build and preflight | PASS: 99 compile-time policy assertions, including eight input encoding/bounds/endianness/EOF checks; secure CA/name/date configuration, five transport units with logging compiled out, credential and unsafe upgrade strings absent, NVS excluded |
| Host/app tests | PASS: 45 preceding tests + seven question checks, including actual Tk countdown/review/save-failure/retry/close workflow; preceding full six-sample calibrator UI regression also passes |
| Question build/flash | PASS: 2,579,888-byte app; guarded development MAC; write hashes verified; calibrated policy confirmed on COM26 |
| Offline q | PASS: rejected before capture or session request |
| Calibrator exclusion | PASS: held calibration control rejects q before capture/upload |
| Early cancellation/guards | PASS: duplicate q, synthetic n, face/cue, greeting and session attempts cannot compete; cancelling local question capture clears data and rearms wake |
| Retained complete capture | PASS: after starting q, Wi-Fi was stopped before completion; the complete four-second question is discarded instead of requesting a session; subsequent normal capture succeeds |
| Ordinary capture recovery | PASS: 64,000 actual samples / 128,000 bytes, shift 15, raw/four-window metadata, local cue and idle after discarded question |
| Actual app USB adapter | PASS: verifies board, checks calibrated policy, connects saved Wi-Fi, pauses wake and closes offline safely, without starting a question |
| Earlier profile | PASS: `microphone` rebuilt at shift 15 with question feature disabled; not flashed over current question firmware |
| Unattended privacy | No provider request or microphone upload in hardware QA; ambient captures were local recovery checks and do not count as human speech samples |
| Human question/answer | PENDING: prompted user to run the Desktop test when back in a quiet room; no timed speech window started while they were away |

One source edit hit Windows' default text-decoding mismatch; it was corrected to explicit UTF-8 before the final rebuild. No dependency or Worker deployment change was needed. Windows Computer Use inspection remains unavailable; UI event-loop/layout checks use isolated demo data and do not claim physical audio confirmation.

Application SHA256: `265b91f4d846c30a4b8d0454b55a66929f9e09fbb733ee5692a98728805170d8`. Pre-question private full-flash backup: `recovery/2026-11-18-phase-4e/before-question-full-flash.bin`, 8,388,608 bytes, SHA256 `76e0895a53b89d55b910cd942b74d4a0796d781e8b0232cb34966372dad816f5`. It preserves the preceding calibrator firmware/NVS. Original backups remain intact. Tested question write files/configuration/manifest are archived in that directory's `built-question/` subdirectory.

Final ignored evidence: `esp32-question-preflash.json`, `esp32-question-build-final.log`, `esp32-question-flash.log`, `esp32-question-local-qa.json`, `robot-question-host-tests.log`, `calibrator-question-regression-ui.log`, and `esp32-question-microphone-regression-build.log` under `logs/`. **Microphone measurements are incorporated; the one spoken question/answer milestone remains unverified until the user's saved test result is reviewed.** See [question procedure](ESP32_QUESTIONS.md).

## 2026-11-18 — Fix session clock and audio-input metadata

The user's closed session `2026-11-18_13-00-31_40ca34fb` contains two complete, unclipped captures at shift 15 but HTTP-200 / upgrade-0 failures (`invalid_session_response`). A session-only request reproduced this without microphone input. A separate hosted metadata probe confirmed the exact five-field response, printable 76-byte temporary credential, matching model/endpoint and expected expiry intervals; no credential value was printed or saved.

The old firmware accepted any clock date after January 2025 and skipped subsequent synchronization. Instrumented status measured the device **216.6 seconds ahead of PC UTC**. This made a fresh token appear past its one-minute new-session start deadline. The session client now requires fresh bounded SNTP synchronization before every temporary-session request, with certificate/name/date and expiry checks kept strict. Added bounded validation reason/relative-time diagnostics without payload/token logging, plus a clock-skew policy assertion.

The clock fix passed session HTTP 200, Gemini upgrade/setup/normal close, and a fixed-text greeting: 130,560 reply bytes / 65,280 played samples / 2.720 seconds, normal closure and READY. That test did not exercise audio-input server messages. The app now verifies setup/close during Connect before enabling Start, saves preflight metadata, and keeps Start disabled on preflight failure.

The user's next run, `2026-11-18_13-16-55_9df30162`, passed that preflight and captured another 64,000 samples with zero clipping. It reached upgrade 101 but failed with `unexpected_server_message`. A synthetic desktop probe, logging only whitelisted message types/counts, observed two `voiceActivity` objects and no unknown types; its four-second input received a response containing the expected answer. A developer-only ESP32 synthetic-input run then exposed `unexpected_input_transcription` after the first parser correction.

The parser now accepts bounded voice-activity objects and, only for audio questions, validates/discards input-transcription metadata. Transcript values are not retained, saved, echoed as prompts or used as audio/completion evidence. Metadata may arrive after completion while EOF finishes; further model audio/turn content remains rejected. Unknown root messages/tools, invalid transcription types, malformed audio, empty/interrupted turns and size limits remain guarded. Ten new audio-metadata assertions plus the clock diagnostic assertion bring the compiled policy count to **110**.

Added a developer-only `-FixtureCheck` path and verified synthetic-fixture preparation. Its explicit f command sends public four-second PCM through the same ESP32 question transport/reply/speaker path without using microphone audio. Normal builds disable that option; the final image excludes the fixture and handler. This regression replaces further blind live retries while protocol handling is investigated.

| Final evidence | Result |
|---|---|
| Normal build/flash | PASS: 2,581,280 bytes, SHA256 `8ed2d6bb5fd6a75793573c7a6b1639f62b0bf4ef1715f0ecb2c20cb7b8c1c245`; guarded COM26 flash and hash verification |
| Policy tests | PASS: 25 session + 16 setup + 42 output/metadata + 19 microphone + eight input assertions = 110 |
| Host/app tests | PASS: 45 preceding tests and eight question tests; new failed-preflight case keeps Start disabled with no recording attempt; complete calibrator Tk regression passes |
| Clock/session/voice probe | PASS: fresh synchronization on all three requests; HTTP 200, upgrade 101/setup/close, fixed greeting playback/READY; no microphone upload |
| Synthetic desktop shape | PASS: 128,000 input bytes / 100 frames / EOF; `voiceActivity` observed; expected answer present; only field-name counts and booleans saved |
| Complete synthetic ESP32 turn | PASS: 128,000 input bytes / 100 frames / EOF; 194,400 response bytes / 97,200 played samples / 4.050 s; playback/settling 4.220 s; request 16.789 s; close 1000, zero clipped output samples, READY |
| Final app preflight | PASS on normal firmware: Worker/Gemini setup before Start, no question/audio input sent, wake paused after success and restored with offline close |
| Security/configuration | CA/name/date checks and strict expiry windows preserved; five transport units still compile logging out; actual device credential absent; NVS preserved; normal image/compile list/cache verified fixture-free |
| Human microphone answer | PENDING: a fresh live retest was requested after the full synthetic audio path succeeded |

Private evidence: `esp32-question-original-session-reproduction.json`, `esp32-question-session-response-shape.json`, `esp32-question-clock-fix-hardware.json`, `robot-question-message-shape.json`, `esp32-question-fixture-input-transcription-failure.json`, `esp32-question-fixture-hardware.json`, `esp32-question-session-parser-fix-preflash.json`, `esp32-question-session-parser-fix-build-full.log`, `esp32-question-session-parser-fix-flash.log`, `robot-question-final-app-preflight.json` and host-test logs under ignored `logs/`. No actual user audio/transcript was saved. User-directed audio attempts remain separate from autonomous session, fixed-greeting and synthetic-input probes.

The normal rebuild had one incomplete CMake error under PowerShell stream redirection; rerunning with full process output completed cleanly. The passing diagnostic image is archived as `built-question-fixture-qa/`, and the final normal image as `built-question-session-parser-fix/`, under the existing private `recovery/2026-11-18-phase-4e/` directory. Earlier images and the verified pre-question full-flash backup remain intact. Worker deployment, keys, microphone gain/window, wiring and dependency versions are unchanged by these fixes.

## 2026-11-18 — Phase 4e live microphone turn verified

The user reran the Desktop test and reported **“spoken question answered by correct spoken result.”** Reviewed the closed, non-demo session `2026-11-18_14-00-56_4f328c47`; its single attempt is `verified_by_user` with `human_confirmed=true`. The saved cloud preflight also passed. Source JSON SHA256: `c8c16a37c32a05048280d953dfdcf6b0e3c26e19a8ba5876583f12fc248cb0c5`.

| Live check | Observed result |
|---|---|
| Microphone | 64,000 samples / 128,000 bytes / four seconds at shift 15; elapsed capture/discard 4.119 s |
| Input level | RMS 1,119; peak 9,035; zero clipped and near-full-scale raw samples |
| Upload | Exactly 128,000 bytes in 100 frames; EOF sent; input cleared |
| Session/turn | HTTP 200, upgrade 101, setup and turn complete, normal close 1000 |
| Output | 182,400 bytes / exactly 91,200 samples played / 3.800 s; zero clipped samples |
| Playback/settling | 3.965 s; complete session/network/playback job 17.106 s |
| Recovery/physical | Client READY/wake gate passed; user selected clear/correct result and confirmed the correct spoken answer in chat |
| Saved data | Measurements and confirmation only; no audio recording or transcript; app closed at 22:01:39 UTC / 14:01:39 PST |

The successful run uses the installed normal firmware SHA256 `8ed2d6bb5fd6a75793573c7a6b1639f62b0bf4ef1715f0ecb2c20cb7b8c1c245`, with the synthetic fixture excluded. Evidence remains in ignored `logs/robot-question-tests/2026-11-18_14-00-56_4f328c47/session.json` and the reviewed numeric summary `logs/esp32-question-live-verified.json`. The original session was not edited. No firmware change or extra hardware test was needed to record this checkpoint.

**Phase 4e passed: real microphone → Gemini → correct spoken answer → READY.** This establishes one complete manual turn at the calibrated input gain. The next small gate is a wake-triggered single turn, followed by separate repeated-cycle and follow-up conversation checks. Normal Hi, E-S-P behavior at that checkpoint remains the local capture/cue routine.

## 2026-11-18 - Phase 5a armed wake checkpoint installed

Added the opt-in **wake** profile and separate Desktop **Robot Wake Question Test**. Connecting verifies Gemini setup without sending microphone audio. Each explicit **Arm next wake** hands the idle calibration hold to one model-recognized Hi, E-S-P event. The arm is consumed before the shared question capture begins, using source `voice_question`; no follow-up turn is added. The established calibrated microphone, input allocation transfer, four-second capture/end cue, temporary session, complete bounded output, original speaker task and READY recovery remain shared with the verified manual path.

An arm has a ten-second heartbeat lease and a hard one-minute waiting window. A late heartbeat cannot revive it. Duplicate arms do not restart deadlines; competing session requests are blocked while waiting. Explicit disarm, cancellation, calibration takeover and Wi-Fi loss clear waiting authorization. Reconnecting Wi-Fi does not rearm. Boot remains offline and unarmed. The manual question tester remains compatible; the separate calibrator stays local-only.

The tester records the arm baseline, new physical wake count and correlated microphone cycle, and rejects mismatched/manual/duplicate trigger traces or incomplete upload/reply/playback. Its allowlist excludes q, f, n and provisioning. Each trial and its separate human audible-result confirmation autosave under `logs/robot-wake-tests/`; demo data is isolated. Stop/close while waiting or recording preserves the partial result. The Desktop shortcut and root launcher are installed, and the user has been prompted to run three independent trials when ready, with no timed chat coordination.

| Verification | Result |
|---|---|
| Build and configuration | PASS: 116 compiled assertions; six new wake permission/deadline policies; CA/name/date checks, five transport units with logging compiled out, actual device credential absent, NVS excluded, fixture disabled |
| Host/app regression | PASS: all 62 tests; new physical-cycle correlation, rejected/expired arms, no manual/provisioning writes, heartbeat ownership, pre-cancelled Start, complete reply, and actual Tk waiting/stop/retry/confirm/save/close workflow; prior manual and full calibrator UI tests pass |
| Flash | PASS: guarded development MAC, verified write hashes, current app 2,583,152 bytes |
| Boot/connection | PASS: offline/unarmed, offline arm rejected; real desktop adapter verifies configured board, capture shift 15 and Gemini setup/normal close |
| Serial ownership | PASS: duplicate arm and competing setup rejected while waiting; disarm returns idle |
| Deadlines | PASS on board: absent heartbeat expires in ten seconds; late heartbeat does not rearm; active heartbeats cannot extend the one-minute window |
| Takeover/network | PASS: calibration control disarms; Wi-Fi stop disarms; offline arm rejected; reconnect leaves arm off |
| Local recovery | PASS: ordinary local capture/cue after those transitions; exactly 64,000 samples at shift 15, elapsed 4.115 s, RMS 623, peak 2,469, zero clipping |
| Unattended provider/input | PASS: one setup-only session request, zero question requests, no microphone audio uploaded; ambient local recovery sample is not a human speech calibration result |
| Physical wake and correct answer | PENDING: three self-paced real trials, saved technical result and human confirmation required |

Windows sharing locks interrupted generated-directory cleanup and Ninja dependency-file removal in the synced project build tree. Added optional `-BuildRoot` to put generated artifacts outside that tree; the complete build then passed at `%LOCALAPPDATA%/ESP32_Companion/build-wake`. The default build location remains unchanged. Disposable unit/UI fixtures now use separate system temporary directories to avoid the same cleanup locks; real user and demo result locations remain unchanged. No vendor asset-script change was retained.

Current app SHA256: `239c8f5189878702d850c8ab57be1471ba423859a29de67f44983e5eb0939ee0`. Private pre-wake full-flash backup: `recovery/2026-11-18-phase-5a/before-wake-full-flash.bin`, 8,388,608 bytes, SHA256 `751c0b8e0de35279416d8c2043e3e397ef7f6446da5c781e99707725ef5a0373`; its OTA0 app was verified byte-for-byte against the preceding passed manual image. Eight write/configuration artifacts and their hash manifest are archived in `built-wake/` beside that backup. Earlier backups and verified images remain intact.

Ignored evidence: `esp32-wake-preflash.json`, `esp32-wake-hardware-qa.json`, `esp32-wake-build-external.log`, `esp32-wake-flash.log`, `robot-wake-host-tests.log` under `logs/`. All ten unattended board checks pass. No Worker deployment, key, provider model, TLS, audio gain/window, wiring or dependency version changed. Physical testing was pending at installation; the following entry records its result. [Self-paced test](ESP32_WAKE.md).

## 2026-11-18 - Phase 5a three real wake turns verified

The user ran the new tester independently and reported **three successful cycles**. Reviewed the closed, non-demo session `2026-11-18_15-40-39_be9779c7`. It contains exactly three attempts, each `verified_by_user` with `human_confirmed=true`. Source SHA256 `da4183efbf230c4360ad9b64b908d403cdd47d66bc53211ec7541c8034232b36`. The original JSON remains unchanged; reviewed numeric summary is `logs/esp32-wake-live-verified.json`.

| Trial | New wake / baseline | Cycle / capture request | Input RMS / peak / clipped | Reply samples / duration | Playback including settling | Session job |
|---|---|---|---|---|---|---|
| 1 | 2 / 1 | 3 | 973 / 6,006 / 0 | 103,440 / 4.310 s | 4.471 s | 18.377 s |
| 2 | 3 / 2 | 4 | 924 / 5,672 / 0 | 53,040 / 2.210 s | 2.378 s | 14.305 s |
| 3 | 4 / 3 | 5 | 844 / 6,003 / 0 | 91,921 / 3.830 s | 4.007 s | 17.256 s |

All three model triggers exceeded their arm baseline and matched their microphone capture cycle. Each capture was exactly 64,000 samples / 128,000 bytes / four seconds at shift 15, with complete 100-frame upload, EOF and input clearing. HTTP 200, WebSocket 101, setup, completed turn, close sent/acknowledged and close code 1000 pass for each reply. Every output sample was played; output clipping is zero in all three. The client's SPEAKING/READY gates passed and the user confirmed correct clear spoken answers after each turn. Connect's setup-only preflight also passed. Session closed at 23:42:16 UTC / 15:42:16 PST, with no recording or transcript saved.

**Phase 5a passed: three separate armed physical wakes -> microphone questions -> correct spoken answers -> READY.** The installed image is unchanged from the verified wake build above. The next baby step is bounded follow-up listening after an answer, then quiet timeout and a fresh wake. The current mode remains one cloud turn per explicit arm.

## 2026-11-18 - Bounded follow-up checkpoint installed

Added the opt-in **followup** profile and Desktop **Robot Follow-up Conversation Test**. Explicit c arms one physical wake for at most two spoken answers on one Gemini connection. The earlier a/q single-turn paths remain available. After each answer and existing output settling, the hardware controller requests another four-second retained capture through the sole microphone input task, source `followup_question`. Local ESP-SR VAD mode 3 examines 200 twenty-millisecond frames; ten positives and a five-frame run are required to upload a follow-up. Quiet segments are wiped and close the session. Speech after the second answer ends at the turn limit without another upload.

The new bounded transport preserves context, validates complete replies, wipes the reply accumulator between turns, and pumps ping/close/metadata during existing output-task playback. A playback helper queues PCM without taking over I2S writing. The same calibrated capture shift 15, wake shift 12, 128,000-byte input, 100 frames/EOF and ten-second output bound remain. The session has a two-minute local deadline and finite setup/upload/capture/reply/close waits. Network-job ownership expires after ten seconds without heartbeats and cannot be revived after cancellation. Controller/task cleanup clears completed producer-owned follow-up PCM as well as session-owned input on cancellation or network failure.

The desktop tester starts no question on Connect. It enables the two-question option only after a successful quiet test and separate human confirmation. Full trials ask the first math question, then a context-dependent follow-up during the next green window, without another click/wake. Capture metrics, each reply's playback, VAD decisions, partial/error/cancelled states and human results autosave under `logs/robot-conversation-tests/`; no recording or transcript is saved. Demo/automated UI data stays isolated. A third trial repeats the full sequence with a fresh physical wake after quiet closure.

| Verification | Result |
|---|---|
| Build policies | PASS: 127 compiled assertions, including eleven frame/count/speech/turn-budget checks; normal profile excludes embedded fixtures |
| Host/app regression | PASS: all 70 Python tests, including the earlier manual/wake testers and full calibrator UI; new eight-test group verifies one connection, exact input/playback, fixture exclusion, heartbeat ownership, quiet-first unlock, saved review, save retry and close during recording |
| Desktop synthetic context | PASS: first and follow-up each send 128,000 bytes / 100 frames + EOF on one connection; expected four and twelve detected; complete audio, setup and close 1000; booleans/counts saved instead of reply audio/transcript |
| ESP32 synthetic full turn pair | PASS: one connection; both complete inputs; first reply 218,402 bytes / 109,201 samples / 4.550 s, playback/settling 4.794 s; second 206,880 bytes / 103,440 samples / 4.310 s, playback/settling 4.554 s; zero output clipping; job 37.253 s; normal close and READY |
| ESP32 synthetic speech/quiet | PASS: follow-up VAD 93 positive frames / run 93; final zero-filled input 0 / 0; quiet buffer discarded without upload |
| Active cancellation | PASS: cancellation after the first synthetic EOF restores READY with at most one input, no follow-up upload and no microphone capture |
| Owner heartbeat loss | PASS: expires after ten seconds, cancels the synthetic job and restores READY; no microphone capture or follow-up upload |
| Normal-image boot/app | PASS: offline and unarmed, offline arm rejected, real adapter checks calibrated policy and Gemini setup/normal close before enabling an arm |
| Normal-image ownership/deadlines | PASS: duplicate arm and competing session rejected; explicit disarm; ten-second waiting lease/late heartbeat; one-minute hard window despite active heartbeats; calibration takeover |
| Normal-image network/local recovery | PASS: Wi-Fi stop disarms, offline rearm rejected, reconnect leaves arm off; subsequent local capture exact 64,000 samples, shift 15, 4.122 s, RMS 608 / peak 1,788 / clipped 0 |
| Unattended room audio | No microphone input uploaded: normal guard checks issued only one setup-only provider request; synthetic protocol/cancel/owner checks used public generated PCM |
| Final secure flash | PASS: guarded development MAC and write hashes; CA/name/date checks retained, five transport units compile logging out, actual device credential absent, NVS excluded; both public PCM fixtures and diagnostic capture strings absent from normal image |
| Human quiet/follow-up/re-wake | PENDING: self-paced Desktop tester installed and user prompted to run one quiet trial, then two full trials |

The synthetic cancellation/owner-loss cases encounter in-flight server audio beyond the bounded close-drain allowance and report `oversized_close_drain`; they release the transport and recover READY rather than waiting indefinitely for a close acknowledgment. Successful turns still require normal acknowledged close. Initial integration exposed a uint32_t/unsigned reference mismatch, corrected before flashing, and the desktop probe needed binary JSON-frame decoding. An initial normal guard launch found COM26 still occupied by flashing; rerunning after the verified flash completion passed all ten checks. These resolved test/build issues do not count as physical speech passes.

Current normal app: **2,592,416 bytes**, SHA256 `9854184e25abb717148410cc790c57be37f2d9258819424b628fb616565c432f`. Final passing synthetic image: 2,849,664 bytes, SHA256 `c6bec0dda56f2d404c8623ca9f48f3f36f6eaffba7604f1697706ec3690620e9`; it is archived separately and is not installed. The private pre-follow-up full-flash backup is `recovery/2026-11-18-phase-5b/before-followup-full-flash.bin`, 8,388,608 bytes, SHA256 `356dba0a32962e44e135f839273209fb333d51cbe2768e738fc73161452e0a28`; its OTA0 app matches the preceding verified wake image byte-for-byte. Normal write/configuration files and hash manifest are preserved in `built-followup/`, synthetic artifacts in separate sibling directories. Earlier backups remain intact.

Ignored evidence: `gemini-followup-context.json`, `esp32-followup-fixture-final-hardware.json`, `esp32-followup-preflash.json`, `esp32-followup-hardware-qa.json`, build/flash logs and `robot-conversation-host-tests.log` under `logs/`. No Worker deployment, API key, model, TLS, microphone gain/window, wiring or dependency version changed. The physical gate was pending at installation; its completed result follows. [Self-paced follow-up procedure](ESP32_FOLLOWUP.md).

## 2026-11-18 - Real quiet and follow-up trials verified

The user completed all three self-paced trials and reported success. Reviewed closed, non-demo session `2026-11-18_16-34-53_8d1f2d33`: exactly quiet, followup, followup, all `verified_by_user` with `human_confirmed=true`. The quiet trial completed before the full option unlocked. Source JSON SHA256 `776655fa92ba6b31941a99b303f8b955607cbacba0658ee23aa305a509ab686c`. Its capture/turn callbacks agree with the final measurements. The original source remains unchanged; reviewed summary is `logs/esp32-followup-live-verified.json`.

| Trial | Physical wake / baseline | Capture requests | Reply samples / duration | Follow-up VAD speech / longest | Quiet VAD speech / longest | Session job |
|---|---|---|---|---|---|---|
| Quiet control | 4 / 3 | 8, 9 | 96,721 / 4.030 s | No second input | 4 / 4, rejected | 23.515 s |
| Full 1 | 5 / 4 | 10, 11, 12 | 71,281 / 2.970 s; 106,561 / 4.440 s | 87 / 29, accepted | 4 / 4, rejected | 40.370 s |
| Full 2 | 6 / 5 | 13, 14, 15 | 49,920 / 2.080 s; 58,801 / 2.450 s | 76 / 53, accepted | 4 / 4, rejected | 34.373 s |

All eight real captures were exactly 64,000 samples / four seconds at shift 15, elapsed 4.112-4.137 s, with zero clipping or raw near-full-scale words. Five spoken questions uploaded **640,000 bytes in 500 frames plus their EOF signals**, with input clearing. Three quiet segments totaling **384,000 bytes were wiped locally without upload**. All five replies completed and exactly **383,284 samples** played, with zero output clipping. Each conversation used one Worker request and one Gemini connection, closed normally, timed out on quiet and passed READY. The user confirmed four for each initial question, twelve for each context-dependent follow-up, clear speech and quiet idle. New wake counts 5 and 6 establish fresh conversations after closure.

Quiet-window RMS was 948, 346 and 1,159; follow-up speech RMS was 775 and 687. VAD accepted both speech windows and rejected all three quiet windows despite overlapping energy levels. The current VAD gate and calibrated gain need no adjustment for these successful trials. No audio recording or transcript was saved. Connect's setup-only preflight also passed. The app closed at 00:37:14 UTC on November 19 / **16:37:14 PST on November 18**.

**Phase 5b bounded follow-up passed:** quiet control plus two complete armed wake/question/answer/follow-up/answer/quiet/READY conversations, including a fresh wake after closure. The installed normal image is unchanged from SHA256 `9854184e25abb717148410cc790c57be37f2d9258819424b628fb616565c432f`. Standalone wake operation without the Desktop arm, longer turn-taking, interruption, prolonged idle and broader network-fault recovery remain later checkpoints.

## 2026-11-18 - Standalone firmware installed; physical gate pending

The opt-in `standalone` profile adds saved-Wi-Fi startup and autonomous physical-wake ownership of the existing two-answer conversation. No Desktop arm, open serial connection or PC heartbeat is required. Boot/network reconnection never initiate a provider request. Offline/unconfigured/cooldown/rate-limited wakes remain local; failed autonomous work starts a 30-second cooldown with no automatic question retry. The same shift-15/four-second microphone capture, local VAD and strict TLS remain. The robot displays ASK NOW after capture warmup.

Controlled Desktop apps acquire a ten-second p pause and renew it with the existing heartbeat. They release it with y on close; expiry restores autonomy after USB/app loss. Temporary diagnostic Wi-Fi changes preserve the previous desired network state. Explicit Desktop c conversations retain their heartbeat requirement. The new **Robot Standalone Test** is status-only and sends no arm, control, network or heartbeat commands. It autosaves one quiet and two full trials, then disconnects serial and presents a saved separate-USB-supply checklist. Human power/voice observations are separate from measured protocol results.

| Verification | Result |
|---|---|
| Build policies | PASS: 139 compiled assertions, including twelve standalone admission/offline/cooldown/pause/expiry cases |
| Host/app regression | PASS: all 80 tests; actual Tk three-trial/confirmation/disconnect/checklist/autosave and layout bounds, read-only commands, physical-wake/autonomous-owner correlation, quiet discard, Stop/close without robot mutation, lease-loss cleanup and partial-old-status USB reopen |
| Synthetic no-PC-owner exchange | PASS: after the first synthetic input EOF, serial remained closed for 45 seconds; the ESP32 completed both replies with owner=standalone and no heartbeat, wiped quiet input, closed normally and returned to READY |
| Synthetic counts | Two accepted inputs / 256,000 bytes, two answers / 97,440 played samples, one quiet discard, HTTP 200 / close acknowledgment / code 1000; no microphone capture |
| Normal boot / fixture exclusion | PASS: saved Wi-Fi and wake ready, zero provider requests; no synthetic f handler or embedded public PCM |
| Diagnostic pause | PASS: twelve seconds of renewal retain control; temporary Wi-Fi stop leaves prior preference intact; explicit release reconnects |
| Lost diagnostic USB | PASS: serial closed for twelve seconds without heartbeat; control expires and saved Wi-Fi reconnects without a provider request |
| Manual offline preference | PASS: o outside diagnostic pause persists until w; y alone does not change the requested network state |
| Calibrator compatibility | PASS: existing calibrator pauses autonomy, takes exactly 64,000 local samples at shift 15 / zero clipping / no upload, and restores autonomous Wi-Fi/wake on close |
| Observer compatibility | PASS: real status-only connection and close; three status queries, zero arms/heartbeats/network commands; robot remains autonomous and ready |
| Secure normal flash/recovery | PASS: development MAC and write hashes; NVS preserved; device credential absent from image; CA/name/date checks retained; five transport units compile private logging out |
| Human standalone speech / unplugged boot | PENDING: self-paced observer and Desktop shortcut installed; user prompted for three trials and separate-USB-supply cold boot/re-wake |

The first synthetic harness run used an incorrect boot-marker pattern; correcting the harness matched the actual standalone mode log. A first normal observer connection then exposed a partial previous-owner status response on USB reopen. Capture clients now anchor to a full policy header before reading identity/configuration. A regression test and hardware rerun pass. Neither issue required a microphone gain or Worker change. Synthetic output establishes protocol/playback completion; audible correctness and actual PC-disconnected cold boot still require the saved human test.

Normal app **2,596,032 bytes**, SHA256 `90c0277461594eef9d143c044725a5cb0b8a0ad9e7b4958d5a4f700f389a81bd`; archived write/configuration/manifest under `recovery/2026-11-18-phase-5c/built-standalone/`. Passing diagnostic app **2,853,008 bytes**, SHA256 `079ec1a96a00a615ff87ca2a2ef7cb77ce298e44e03bda52d7e3077dd9166f8b`, archived separately with automatic cloud wake disabled and not installed. Diagnostic compile metadata was omitted from its archive because normal reconfiguration had started; its preserved app matches the preflash verified hash. Normal archive includes its complete final compile metadata.

The pre-standalone private full-flash backup is **8,388,608 bytes**, SHA256 `7f78a1f2632da24f51c66aac466cb3c47fc65fb6604d0e81d1cc971c29507e42`; its OTA0 image matches verified Phase 5b byte-for-byte. Existing backups remain untouched. Source/configuration/docs/tools/recovery remain in the project; only disposable generated output builds under `%LOCALAPPDATA%\ESP32_Companion\build-standalone`. The earlier sharing locks have no unresolved source relocation or runtime dependency. The locked-file cause was not proven to be Dropbox.

Ignored evidence: `logs/esp32-standalone-preflash.json`, `logs/esp32-standalone-fixture-preflash.json`, `logs/esp32-standalone-fixture-hardware-qa.json`, `logs/esp32-standalone-hardware-qa.json`, `logs/robot-standalone-host-tests.log`, build/flash logs and future user sessions under `logs/robot-standalone-tests/`. No real microphone recording, transcript or credential was saved. No unattended microphone audio was uploaded. This is the first standalone gate; ten repeated conversation/idle cycles, prolonged idle, access-point outage, longer turn-taking and interruption remain open. [Self-paced procedure](ESP32_STANDALONE.md).

## 2026-11-18 - Standalone voice and separate-USB-power functional gate verified

The user reports all required trials and the unplugged power check passed. Reviewed closed, non-demo session **2026-11-18_17-46-59_cc36a560**, SHA256 `fab8d45e76d2c0e5e94bc67f510100264093855576f78b498943ddb847f77340`. Its original JSON is unchanged; numeric validation/derived summary is `logs/esp32-standalone-live-verified.json`. The session has six attempts, including additional repeats and one actual upload failure, so it does not establish an uninterrupted reliability pass.

| Saved attempt | Exercise / result | Physical wake | Capture requests | Complete answers / played samples |
|---|---|---|---|---|
| 1 | Quiet, verified by user | 1 | 2, 3 | 1 / 101,761 |
| 2 | Full, verified by user | 2 | 4, 5, 6 | 2 / 203,522 |
| 3 | Input upload failed, no complete reply | 3 | 7 | 0 / 0 |
| 4 | Full, verified by user; fresh-wake recovery | 4 | 8, 9, 10 | 2 / 106,800 |
| 5 | Full, verified by user; another fresh wake | 5 | 11, 12, 13 | 2 / 164,162 |
| 6 | Protocol complete and confirmed before later observer USB error | 6 | 14, 15, 16 | 2 / 164,641 |

Attempts **1, 4 and 5** provide the quiet + two full functional gate: eight exact four-second/64,000-sample captures at shift 15, five complete accepted inputs totaling **640,000 bytes**, **372,723 played samples**, and three quiet segments totaling **384,000 bytes discarded locally**. Full-trial follow-up VAD accepts 65/16 and 77/57 speech/run frames; quiet decisions 4/4, 0/0 and 4/4 reject. Each successful result establishes autonomous physical model wake, no PC owner heartbeat, one connection, complete playback, normal close, quiet timeout and READY. Observer counters show zero arms, heartbeats or network commands. Correct clear speech is separately confirmed by the user.

All five protocol-complete, human-confirmed attempts total **nine answers / 740,886 played samples**, **1,152,000 complete accepted input bytes** and **five locally discarded quiet segments / 640,000 bytes**. All fifteen measured captures and all nine completed outputs have zero clipping; no raw near-full-scale input words. Failed-attempt partial-upload bytes are unknown and excluded from those accepted-input totals. The measured elapsed times and duplicate capture/reply/VAD callback fields agree with the final results. No gain/VAD adjustment is warranted by these successful speech/quiet checks.

The unplugged checklist is `verified_by_user`, `observer_disconnected=true`, `human_only=true`, all three checks true and `human_confirmed=true`. It confirms cold boot from a separate USB supply without PC USB, four then twelve on one wake, quiet READY, and another fresh wake/question. This is human evidence because serial was disconnected. Closed at **17:56:33 PST on November 18** / 01:56:33 UTC on November 19.

**Open reliability issue:** attempt 3 reports HTTP 200 / upgrade 101 / `audio_send_failed`. Failure occurs in the upload path before any completed answer; byte progress and the lower-level cause were not saved, so the cause remains undetermined. Later fresh wakes pass, including consecutive full attempts 4 and 5. No automatic retry or firmware change was introduced to hide the failure. The next gate is ten conversation/idle cycles with explicit failure/recovery accounting, then prolonged idle and network-outage checks.

**Resolved Desktop reporting bug:** attempt 6 had a complete successful result and human confirmation at 01:52:41 UTC, before a COM26 access error at 01:52:58. The old app overwrote the confirmed trial's state with `error`. Its metrics and earlier confirmation remain in the unmodified source; the review lists it separately from the clean gate trials. The shared conversation app now records idle connection errors separately and preserves completed, review-pending and confirmed trials. Active-trial errors still fail the trial. All **36 robot/app regression tests** pass after the fix, including two new Tk regression cases (three completed/review states plus an active failure); the earlier full 80-test installation suite remains recorded above.

**Standalone functional gate passed with fresh-wake recovery; robustness remains open.** Installed firmware stays at SHA256 `90c0277461594eef9d143c044725a5cb0b8a0ad9e7b4958d5a4f700f389a81bd`; no flash, Worker, model, TLS, gain or wiring changes were needed for this review. [Detailed results](ESP32_STANDALONE.md).
