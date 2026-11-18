# Device authentication checkpoint

Phase 3b adds a small **POST `/device/check`** route. It proves that a caller knows the one development robot's shared credential. `/health` remains public. This diagnostic route makes no Gemini requests and stores no voice. The current service also has [verified Gemini session issuance](GEMINI_SESSIONS.md); ESP32 cloud firmware remains later work.

Status on 2026-11-17: **local and hosted authentication checks pass**. After the user synchronized the Production Secret with the current local value, hosted missing/wrong credentials returned 401 and the correct credential returned 200. Public health still passes. This proves the desktop-to-Worker authentication checkpoint; Gemini and ESP32 cloud integration remain later work.

## Publish the current service

The original authentication-only increment was pasted into the dashboard editor. Current code/configuration includes a Durable Object binding and two required Production Secrets, so deploy them together with the already-authorized wrapper:

```powershell
.\tools\worker.ps1 -Action test
.\tools\worker.ps1 -Action check
.\tools\worker.ps1 -Action deploy
```

The existing Production Secret supplies `env.DEVICE_AUTH_TOKEN`; its value is not part of the source file. [Current configuration and hosting](CLOUD_WORKER.md).

Opening `/device/check` in a browser sends GET and receives **405**. Use the desktop checker for the authenticated POST test; `/health` remains the browser check.

## Verify from this PC

From the project root:

```powershell
python .\tools\check_device_auth.py
```

The default target is `https://esp32-companion-control.stiener.workers.dev`. The script reads the current credential from the ignored local `cloudflare-worker/.dev.vars` file and sends it in an Authorization header over certificate-validated HTTPS. It rejects redirects and prints/logs only test labels and statuses. It does not print the token or remote response contents.

Expected results:

```text
PASS missing credential: HTTP 401
PASS incorrect credential: HTTP 401
PASS correct credential: HTTP 200
```

The successful response is `{"authenticated":true}`. Both rejected requests return `{"error":"unauthorized"}` with a Bearer challenge. All responses use `Cache-Control: no-store`. The wrong credential is made by changing one character of the local value, rather than relying on a fixed test string.

Useful failure clues:

| Result | Check |
|---|---|
| 404 | Deploy the updated `index.js`; the older health-only version has no device route |
| 503 | Check the Production secret's name/64-character format and the required DEVICE_RATE_LIMITER binding/backend |
| 401 for the correct-credential test | The Production secret and current local token do not match |
| 429 | The authenticated device allowance is spent; wait the Retry-After delay before retrying |

The first hosted run reached that last condition: all three POST requests returned 401. Re-saving the existing Production Secret from the current local file resolved it, and all three cases now pass. For a future mismatch, synchronize the same way and close the private file before replying. There is no need to send its value in chat.

## Local development

```powershell
.\tools\worker.ps1 -Action test
.\tools\worker.ps1 -Action check
.\tools\worker.ps1 -Action dev
```

With the local server running, use another terminal:

```powershell
python .\tools\check_device_auth.py --base-url http://127.0.0.1:8787
```

Stop the development server with **x** when finished. The local tests use a public fixture credential, not `.dev.vars`. The real HTTP test uses the actual local credential; neither test prints it.

## Request contract and limits of this checkpoint

`POST /device/check` expects `Authorization: Bearer <DEVICE_AUTH_TOKEN>`. The token must match the generated 64-character value exactly; the Bearer scheme is case-insensitive. Tokens in the URL or body are not accepted. The Worker validates its configured secret before comparing fixed-length bytes with Cloudflare's timing-resistant comparison. Missing/malformed configuration rejects access with 503.

This is one shared credential for one development device. It is separate from a Cloudflare account token, Gemini API key and Gemini temporary token. Shared request limits and desktop Gemini session access are now verified online; see [RATE_LIMITS.md](RATE_LIMITS.md) and [GEMINI_SESSIONS.md](GEMINI_SESSIONS.md). Device-side transport and a robot conversation remain later checks.

The first locally generated credential was rotated after IDE-selected text exposed it in chat, before the user confirmed saving the replacement. Its value is absent from tracked source/docs/logs. Keep the private file closed when sending IDE context.

Official reference for the comparison: [Cloudflare Web Crypto timingSafeEqual](https://developers.cloudflare.com/workers/runtime-apis/web-crypto/#timingsafeequal). See [CLOUD_WORKER.md](CLOUD_WORKER.md) for hosting setup and [TEST_LOG.md](TEST_LOG.md) for measured results.
