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
