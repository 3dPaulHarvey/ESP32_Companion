# ESP32 authenticated session checkpoint

Phase 4a **passed on the development ESP32 on 2026-11-17**. COM26 rejects missing/wrong credentials, receives a valid temporary Gemini session response over certificate-verified HTTPS, and keeps local capture/cue recovery working during the request. The user confirms normal eyes, English wake, listening and cue behavior after flashing.

This is a control-request checkpoint. The firmware validates and discards the temporary response; it does not open Gemini's WebSocket or upload microphone audio yet.

## Current robot behavior

The new **session** build inherits local diagnostics: startup faces/tones, blinking READY eyes, and **Hi, E-S-P -> LISTENING -> two-second local capture -> CUE -> READY**. Wi-Fi starts **off** at boot. Wake words make no cloud requests. Existing local capture remains in RAM and is cleared after measurement.

USB serial adds three manual commands:

| Command | Action |
|---|---|
| `w` | Start Wi-Fi using the board's saved credentials |
| `g` | Make one authenticated `/device/session` request |
| `b` | Check rejection using an altered copy of the device credential |
| `u` | Check rejection with no credential |
| `o` | Stop Wi-Fi |
| `s` | Show local status, memory and control-profile/device identity |

Each session request runs in a separate task. Duplicate requests while busy are rejected, and the local display/audio loop continues. Boot and wake do not trigger retries or provider calls. After verification, the checker rebooted the device into its normal Wi-Fi-off startup.

## Repeatable build and hardware check

From the project root:

```powershell
.\tools\idf.ps1 -Action build -Profile session
.\tools\idf.ps1 -Action flash -Profile session -Port COM26
python .\tools\test_esp32_session_tool.py
python .\tools\check_esp32_session.py --reset
```

The flash wrapper verifies **COM26 / 44:bd:8d:ec:38:e0** before writing. A different port or board fails the session-profile flash guard. The separate output directory is `build-session/`, with generated `sdkconfig.session.generated`. Baseline and diagnostics profiles remain available.

On a board without the credential, or after rotating it, use:

```powershell
python .\tools\check_esp32_session.py --reset --provision
```

The tool first verifies the profile protocol and development-board MAC, then reads the existing ignored `.dev.vars` device credential and sends a non-echoed USB frame. The firmware accepts only one complete 64-character lowercase-hex value, rejects malformed/partial/oversized/timed-out frames, and writes it to the `companion` NVS namespace as `device_auth`. Frame timeout drains input until newline, so leftover credential bytes cannot execute menu commands. The checker never places the value in command arguments, output or raw serial logs.

The credential survives resets and normal application flashing. This development profile stores it in NVS and does not add encryption at rest. The permanent Gemini API key remains a Cloudflare Secret and never enters the ESP32. The application binary was checked for absence of the real device credential before provisioning.

## HTTPS and response policy

The device posts an empty body only to the fixed `https://esp32-companion-control.stiener.workers.dev/device/session` endpoint. It uses an Authorization header, the ESP-IDF certificate bundle, hostname verification and certificate validity-date checking. SNTP sets the clock when needed, with a 15-second wait limit. The HTTP phase has a 15-second deadline and bounded reads. Redirects are not followed.

The build refuses insecure TLS, disabled certificate/date support or DEBUG/VERBOSE maximum logging. INFO-or-lower logging avoids compiled-in HTTP header dumps. The profile explicitly enables certificate-date checking, which was disabled in the previous diagnostic configuration. [ESP-IDF HTTPS/streaming client](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/protocols/esp_http_client.html), [SNTP service](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/network/esp_netif_programming.html#sntp-service).

Success requires HTTP 200, JSON/no-store/current-revision headers and the exact five-string response contract from [GEMINI_SESSIONS.md](GEMINI_SESSIONS.md). The flat parser rejects duplicate/unknown fields, nesting, escapes, controls, wrong model/endpoint, oversized values and invalid/expired timestamps. Temporary credentials are limited to 8 KiB, the body to 16 KiB, and the expected one-minute start/five-minute expiration windows are checked against the device clock. The parser borrows response bytes without making a credential copy. Owned credential/Authorization/response buffers are cleared before task exit; the HTTP client is cleaned up. Only sizes, HTTP status, timing and result labels are logged.

Negative checks require the exact 401 unauthorized response and Bearer challenge. Network/configuration errors fail closed. A 429 response honors a valid 1-60-second Retry-After window without automatic retry. Forced device throttling/recovery was not part of this run; shared hosted limiter behavior was previously verified in Phase 3c.

## Results and limits

| Check | Result |
|---|---|
| Session build | PASS; 2,563,424-byte application, 17% app-partition space free |
| Policy checks | PASS: 23 compile-time assertions covering credentials, timestamps, JSON/schema/expiry and frame handling |
| Host-tool tests | PASS: six offline identity/privacy/fault/bounds/result-gate tests |
| Wi-Fi-off requests | PASS: rejected locally, HTTP 0, no cloud request |
| Private provisioning | PASS: incomplete frame rejected; correct frame saved; credential survives reboot |
| Incorrect / missing credential | PASS: 401 / 401; duplicate-request busy guard also passes |
| Correct credential | PASS: two verified 200 responses, about 3.95 and 3.36 seconds |
| Concurrent local cycle | PASS: exact 32,000-sample capture/cue/idle recovery during a successful HTTPS request |
| Offline recovery | PASS: Wi-Fi-off capture/cue/idle cycle and quiet reboot |
| Physical follow-up | PASS: user confirms eyes, wake, listening and cue |
| Memory | Observed internal minimum 67,391 bytes; active-Wi-Fi snapshots 86,103-89,243 bytes free; no panic detected in the bounded run |
| Earlier profile | PASS: diagnostics profile rebuild after the shared CMake change |

This short run does not establish an extended memory soak, direct Gemini connectivity, audio upload or live conversation. Microphone headroom and the two-second capture duration remain open before streaming; the successful desktop fixture was 2.41 seconds.

Evidence is in ignored `logs/esp32-session-build-final.log`, `esp32-session-flash-final.log`, `esp32-session-hardware-verified.log`, `esp32-session-build-metadata.json` and `esp32-session-diagnostics-regression-build.log`. The full pre-change flash backup is preserved at `recovery/2026-11-17-phase-4a/before-session-full-flash.bin`, verified as 8,388,608 bytes with SHA256 `b9402e8fd52be5176e8cd421837098fc1d8e4f5eee6807b86ef933f1b7e68f9a`. The original November 9 backup remains preserved separately.

Next: open the direct Gemini constrained WebSocket on the ESP32 with a temporary token and verify setup acknowledgment/cleanup, then add bounded audio exchange. Keep the local diagnostics and recovery paths available throughout.
