# Gemini session checkpoint

Phase 3d is **verified online on 2026-11-17**: the authenticated Worker obtains a temporary token, and the desktop checker uses it to receive Gemini's WebSocket `setupComplete` acknowledgment. This check sends no prompt or microphone audio. It establishes session access; spoken replies and ESP32 cloud transport are the next checkpoints.

The current Worker revision is `gemini-session-v1`, deployed as version `b9a750ce-b8db-464f-8d84-fa4b3b1fe901` at the existing `esp32-companion-control.stiener.workers.dev` address. The robot still runs the verified local wake/listen/end-cue/idle sequence.

## Key setup

The user created a Gemini key in [Google AI Studio](https://aistudio.google.com/apikey) and reported that its project shows **Free tier**. Google's current [pricing table](https://aistudio.google.com/docs/pricing#gemini-3.8-live) lists free-tier Gemini 3.8 Live input/output. The successful hosted token and connection checks confirm access at this checkpoint; they do not measure the remaining project quota. Check the project's AI Studio usage/rate-limit view before longer voice tests. Free-tier content may be used to improve Google's products, as described in that pricing table and its linked terms.

The Production binding `GEMINI_API_KEY` is now **Secret**, alongside the existing `DEVICE_AUTH_TOKEN`. The initial dashboard save created a plain-text variable; its type was identified using metadata and converted with a version update before the session service was deployed. The conversion kept the value out of chat, source files, command arguments and local credential files. A private exact-value audit found no matches in the inspected Wrangler/project logs. Cloudflare version history retains earlier versions; this conversion describes the active Production binding.

For a replacement key, use Cloudflare **esp32-companion-control -> Settings -> Runtime variables and secrets -> Production**, edit `GEMINI_API_KEY`, choose **Secret**, paste directly from AI Studio and deploy. Report only the save status. The value belongs neither in this repository's `.dev.vars` nor in the ESP32 firmware.

## Session contract

Send an empty **POST `/device/session`** with the existing `Authorization: Bearer <DEVICE_AUTH_TOKEN>` header. The Worker authenticates first, then uses the same shared 10-requests-per-rolling-minute allowance as `/device/check`. Request bodies and query options are ignored; the client cannot choose a provider URL, model, lifetime, tools or system instruction.

The Worker calls Google's fixed HTTPS `v1beta/auth_tokens` endpoint with the permanent key in an `x-goog-api-key` header. It requests:

| Setting | Policy |
|---|---|
| Model | `models/gemini-3.8-live` |
| New-session uses | 1 |
| Deadline to start a session | 60 seconds from issuance |
| Token expiration | 5 minutes from issuance |
| Output | Audio, with output transcription enabled |
| Reply length | At most 256 output tokens per reply; instructed to use one or two short English sentences |
| Tools / session resumption | Not enabled |
| Configuration lock | Complete `bidiGenerateContentSetup`, no field mask |

The response includes only `token`, `model`, `expires_at`, `new_session_expires_at` and a credential-free `websocket_url`. The token is sensitive and remains in the checking process's memory. The permanent key is never returned. All responses use `Cache-Control: no-store`.

The REST setup shape was checked against Google's live [discovery schema](https://generativelanguage.googleapis.com/$discovery/rest?version=v1beta), which names the field `bidiGenerateContentSetup`; the tutorial currently shows a different shape. The authoritative schema states that supplying a complete setup without a field mask replaces the client's setup. The real constrained WebSocket handshake passed with this REST request. See also [ephemeral tokens](https://aistudio.google.com/docs/live-api/ephemeral-tokens), [WebSocket authentication](https://aistudio.google.com/docs/live-api/get-started-websocket#authentication-with-ephemeral-tokens) and the [current model](https://aistudio.google.com/docs/models/gemini-3.8-live).

## Repeat the check

From the project root:

```powershell
.\tools\worker.ps1 -Action session-check
```

The checker reads only the existing device credential from ignored `.dev.vars`. It verifies GET=405, missing/wrong credential=401, valid temporary-token issuance=200, expected expiry/model/response fields, and a direct Gemini constrained-WebSocket setup acknowledgment. It closes immediately after acknowledgment. The temporary credential is added to Google's required `access_token` query parameter only in memory; the URL, token, provider messages and close reason are never logged. Stored results contain controlled pass/status metadata.

For issuance without a WebSocket connection, or a different log name:

```powershell
& .\.tools\node-v24.22.3-win-x64\node.exe .\tools\check_gemini_session.mjs --name worker-gemini-token-only
```

These checks make real provider requests and spend authenticated control admissions. Keep them deliberate; do not loop while a quota response is active.

## Failures and local checks

| Result | Meaning / next action |
|---|---|
| 401 | Missing/wrong device credential; synchronize it using [DEVICE_AUTH.md](DEVICE_AUTH.md) |
| 503 `service_unavailable` | Check the required Secret names/format and Durable Object binding |
| 429 `rate_limited` | Shared Worker admission allowance spent; wait `Retry-After` |
| 429 `provider_rate_limited` | Google returned 429; wait and check project quota; no automatic retry |
| 502 `provider_unavailable` | Provider rejected issuance or returned an invalid response; `X-Companion-Provider-Status` supplies only Google's HTTP status when available |
| 504 `provider_timeout` | Provider request did not complete within 10 seconds |
| Token succeeds, setup fails | Check Live model/project access, quota and network; issuance alone is not a connection pass |

Google error bodies are not passed through. Responses are bounded to 16 KiB, token strings to 8 KiB, redirects are not followed and no automatic retries occur. Public health remains independent of Gemini availability.

```powershell
.\tools\worker.ps1 -Action test
.\tools\worker.ps1 -Action check
```

All **20 workerd runtime test groups** pass with public fixture credentials and a fake outbound provider. They cover existing health/auth/quota behavior, shared admissions across both device routes, server-owned setup, token response filtering, provider failures/quota, malformed and oversized responses, redirect refusal and a real 10-second timeout. Local tests use no real Gemini key or Google connection. The real hosted checker separately proves the Google connection.

Real local HTTP also passes health=200, missing session auth=401 and valid session auth without a local Gemini key=503. Wrangler's missing-key warning is expected for local health/auth work; the real key stays in Production. The local server was closed after checking.

Evidence: `logs/worker-gemini-runtime-tests.log`, `worker-gemini-bundle.log`, `worker-gemini-session-check.log`, `worker-gemini-hosted-auth-regression.log`, `worker-gemini-local-http.log` and `worker-gemini-deployment.json`. See [TEST_LOG.md](TEST_LOG.md) for the recorded results.

## Next baby step

The first fixed text request / spoken response and a prerecorded 16 kHz question / 24 kHz answer have now passed, including user-confirmed desktop playback. [Audio exchange results](DESKTOP_AUDIO.md). Next prove authenticated Worker/session access on COM26, then add embedded audio transport. Finish microphone headroom and capture-duration checks before live robot conversation. No ESP32 cloud firmware or personal voice upload is part of Phase 3d.
