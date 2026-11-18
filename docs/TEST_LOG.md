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
