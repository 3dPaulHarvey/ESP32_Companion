# ESP32 direct Gemini setup checkpoint

Phase 4b **passed on COM26 on 2026-11-17**, including the user's local behavior check. The separate **gemini** profile requests a temporary session from the Worker, opens Google's secure WebSocket, receives `setupComplete`, exchanges close frames, and releases the connection. The permanent Gemini key stays in the existing Cloudflare Secret. No additional Cloudflare configuration is needed for this step.

COM26 has since advanced to [Phase 4c's manual speaker profile](ESP32_VOICE.md). This page preserves the setup-only commands and historical Phase 4b results.

## What the robot does

Boot remains Wi-Fi off, with the existing startup faces/tones and blinking READY eyes. Saying **Hi, E-S-P** still starts **LISTENING -> two-second local capture -> CUE -> READY**. Wake words do not start a cloud connection in this profile. This checkpoint gives no spoken Gemini answer.

The new USB serial command is **`j`**. First send `w` to connect using the saved Wi-Fi credentials, then `j` to run one setup/close check. Its result appears as controlled serial metadata, rather than a new face or sound. The existing `g` session-only, `b` incorrect-credential, `u` missing-credential, `o` Wi-Fi-off and `s` status commands remain available. Duplicate requests while busy are rejected; failed connections do not automatically retry.

The checker also triggers a local 32,000-sample capture while a connection opens. That capture remains owned by the local audio worker and is not passed to the network task.

## Build and repeat the check

From the repository root, close other serial monitors and run:

```powershell
.\tools\idf.ps1 -Action build -Profile gemini
.\tools\idf.ps1 -Action flash -Profile gemini -Port COM26
python .\tools\test_esp32_session_tool.py
python .\tools\test_esp32_gemini_tool.py
python .\tools\check_esp32_gemini.py
```

The flash guard permits only COM26 with development MAC `44:bd:8d:ec:38:e0`. Output is isolated in `%LOCALAPPDATA%/ESP32_Companion/build-gemini/`, including its generated `sdkconfig`; `-Profile session` restores the preceding session-only checkpoint. Normal application flashing preserves NVS. If the device credential has not been provisioned or has been rotated, use the private provisioning procedure in [ESP32_SESSIONS.md](ESP32_SESSIONS.md).

The hardware checker verifies profile/identity, quiet boot, Wi-Fi-off rejection, existing 401/200 behavior, three direct setup/close cycles, the busy guard, concurrent local capture, offline recovery, and quiet reboot. It leaves Wi-Fi off. It sends no question or audio message, prints no raw serial output, and saves only controlled result/heap metadata under ignored `logs/`. Failures also retain that bounded metadata. The requests share the existing ten-per-minute Worker admission limit.

## Connection and privacy rules

The verified Worker response must name the exact expected model and constrained endpoint, contain a bounded temporary credential, and pass the existing schema/expiry checks. The response body owns the borrowed credential view until the connection closes. The Worker HTTP/TLS client and device Authorization buffer are released before the Gemini transport is allocated.

The client uses ESP-IDF v5.5.2's built-in WebSocket/TLS transports, with no added component dependency. It fixes the host to `generativelanguage.googleapis.com:443`, uses the certificate bundle, checks hostname and certificate dates, percent-encodes the temporary token into the required in-memory `access_token` query parameter, and accepts only a verified HTTP 101. Redirects are not followed. Setup contains only the fixed model; the temporary token's server-owned policy supplies the locked session settings. [Google's WebSocket guide](https://aistudio.google.com/docs/live-api/get-started-websocket), [Live API setup acknowledgment](https://aistudio.google.com/api-reference/live).

Transport operations have configured timeouts; setup waits within a 20-second loop and closing within a two-second loop. WebSocket messages are capped at 16 KiB and controls at 125 bytes. Frame tracking handles partial reads, fragmented messages and interleaved ping/pong. The SDK masks outgoing buffers in place, so setup/control payloads use writable storage. Success requires an exact setup acknowledgment followed by a close acknowledgment with no status or normal code 1000/1001. Data already in flight after our Close is drained without interpreting it, within an aggregate 16 KiB bound and the closing deadline. Such data cannot substitute for the close acknowledgment.

The pinned SDK's WebSocket error logging can include its complete upgrade request. This profile compiles logging out of all five `tcp_transport` source units and disables its WebSocket tag at runtime. Pre-flash checks confirm the unsafe request-dump strings are absent from the firmware. INFO-or-lower and secure-TLS compile guards remain in force. The host refuses credential-bearing transport output without copying it to its result log.

Owned response/path/message/scratch buffers are cleared and released, and both WebSocket and TLS handles are destroyed before a successful result is logged. SDK-owned copies are normally freed; this does not promise forensic erasure of every library allocation. The device credential remains in ordinary development NVS, as documented in Phase 4a.

## Verification

| Check | Observed result |
|---|---|
| Build and guarded flash | PASS: 2,563,024-byte application; 17% app-partition space free; COM26 identity and written hashes verified |
| Policy assertions | PASS: 24 session/provisioning assertions and 16 URI/setup/framing assertions |
| Host-tool tests | PASS: seven identity/privacy/session tests and six direct-connection result-gate tests |
| Secure configuration | PASS: CA/name/date checks, encoded bounded token path, unsafe upgrade-request logs absent from the image |
| Existing authentication | PASS: incorrect/missing credential return 401; session-only request returns verified 200 |
| Direct Gemini | PASS: six consecutive final cycles across a full check and a short repeat; each verifies HTTP 101, a 26-byte setup acknowledgment, and close code 1000 |
| Direct transport timing | 1.915-2.240 seconds, excluding the preceding Worker request |
| Local work during connection | PASS: exact 32,000-sample/64,000-byte local capture, cue and idle recovery; duplicate cloud command rejected as busy |
| Offline and reboot behavior | PASS: Wi-Fi-off rejection, local capture recovery, credential retained, quiet Wi-Fi-off reboot |
| Physical follow-up | PASS: user confirms eyes, wake, listening and cue all work |
| Memory | Lowest observed internal free watermark 53,743 bytes; active-Wi-Fi snapshots 74,971-86,639 bytes free; no panic detected in the bounded runs |
| Session-only regression | PASS: earlier profile rebuild; not flashed over the current Gemini profile |

The initial close checker rejected a data message arriving after setup/Close; bounded draining fixed that case. One subsequent run failed after two successful closes before detailed failure metadata was retained. It was not reproduced in the six final consecutive passes. The improved checker now retains controlled stage/status data on failure. These short checks establish this setup checkpoint; extended stability and network-fault testing remain open before live conversation.

Application SHA256: `892a7c5ebd62dc581374e007a424727aaa7881e4528edeaa11ac4a30ebf4405a`. Evidence is in ignored `logs/esp32-gemini-setup-preflash.json`, `esp32-gemini-setup-build-close-drain.log`, `esp32-gemini-setup-flash-close-drain.log`, `esp32-gemini-setup-hardware-verified.log`, `esp32-gemini-setup-short-repeat.log`, `esp32-gemini-setup-host-tests.json`, `esp32-gemini-setup-physical-followup.json` and `esp32-gemini-session-regression-build.log`. Worker code/deployment and dependency versions are unchanged.

The pre-change full-flash backup is `recovery/2026-11-17-phase-4b/before-gemini-full-flash.bin`: 8,388,608 bytes, SHA256 `9550a5988dee3a54b64dd0ffaf9175632af4f3e711cb4e4e67f3f9ef84d38e0e`. It contains the development NVS credential and remains private/ignored. The original November 9 recovery image remains preserved separately.

Bounded playback of a fixed Gemini reply through the robot's speaker is now [verified in Phase 4c](ESP32_VOICE.md). Microphone headroom and capture duration still need verification before live audio upload; the earlier successful desktop audio fixture lasted 2.41 seconds.
