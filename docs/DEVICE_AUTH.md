# Device authentication checkpoint

Phase 3b adds a small **POST `/device/check`** route. It proves that a caller knows the one development robot's shared credential before we implement Gemini session issuance. `/health` remains public. This increment makes no Gemini requests, stores no voice and does not change the ESP32 firmware.

Status on 2026-11-17: the user confirms that `DEVICE_AUTH_TOKEN` was saved as a **Production Secret**. The new handler passes local runtime, HTTP and packaging checks. **Hosted authentication verification is pending deployment of the updated code.**

## Publish this increment

1. Open `cloudflare-worker/src/index.js` in VS Code and copy the entire file.
2. In Cloudflare, open `esp32-companion-control` → **Edit code**. Replace all of `worker.js` with the copied contents.
3. Click **Deploy**. The existing Production secret supplies `env.DEVICE_AUTH_TOKEN`; its value is not part of the source file.
4. Report that the updated code is deployed. The desktop checker can then verify the hosted behavior.

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
| 503 | Production secret is absent or malformed; verify its exact name and 64 lowercase hex characters without surrounding quotes |
| 401 for the correct-credential test | The Production secret and current local token do not match |

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

This is one shared credential for one development device. It is not a Cloudflare account API token, a Gemini API key, or a Gemini temporary session token. Session issuance and request limiting are still later work; add limits before enabling provider calls. Device-side storage and transport remain separate firmware work. No complete Phase 3 exit gate or robot conversation is claimed here.

The first locally generated credential was rotated after IDE-selected text exposed it in chat, before the user confirmed saving the replacement. Its value is absent from tracked source/docs/logs. Keep the private file closed when sending IDE context.

Official reference for the comparison: [Cloudflare Web Crypto timingSafeEqual](https://developers.cloudflare.com/workers/runtime-apis/web-crypto/#timingsafeequal). See [CLOUD_WORKER.md](CLOUD_WORKER.md) for hosting setup and [TEST_LOG.md](TEST_LOG.md) for measured results.
