# Device request limits

Phase 3c sets a threshold of **10 authenticated control requests per 60 seconds per Cloudflare location**, initially on `POST /device/check`. It is intended to slow an accidental retry loop before we enable Gemini session issuance. Voice audio will use a separate Gemini WebSocket, not this control endpoint.

Status on 2026-11-17: local runtime, packaging, HTTP burst and recovery checks pass. **Hosted deployment and verification are pending Wrangler account authorization.** The live service's last verified checkpoint is device authentication without this limiter.

## Behavior

- Correct credentials are checked before the device quota. Missing/wrong credentials still receive 401 and do not spend that quota.
- An admitted request returns the existing `{"authenticated":true}` response.
- A throttled request returns **429**, `{"error":"rate_limited"}`, `Retry-After: 60`, and `Cache-Control: no-store`. Wait before retrying; the header is a conservative retry hint, not a precise reset timestamp.
- `/health` stays public and available when the device is throttled.
- Missing/invalid/unavailable limiter configuration rejects authenticated requests with 503. It does not fall back to an in-memory counter or unbounded access.
- One fixed, nonsecret counter key represents the one development device. Changing query parameters does not create another allowance. The device token is not used as the key or included in output.

Cloudflare documents these limits as approximate and local to each location, with counters synchronized in the background. They are **not a strict worldwide quota or billing cap**. Before Gemini calls, also verify provider quotas and handle provider 429 responses. The normal Worker invocation allowance still applies to rejected and health requests. [Official rate-limiting API](https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/)

## Configuration

`cloudflare-worker/wrangler.jsonc` declares `DEVICE_RATE_LIMITER`, namespace `20261117`, and `simple: {"limit":10,"period":60}`. Reserve that namespace for this project in this account; sharing namespace and key shares counters. The configuration also requires the existing `DEVICE_AUTH_TOKEN` secret and retains enabled Worker observability and disabled preview URLs shown by the remote configuration.

Rate-limiting bindings are not currently visible in the Cloudflare dashboard. Deploy this configuration with Wrangler together with the code. Pasting the new code alone into the editor would omit its required limiter binding and cause authenticated requests to return 503. [Cloudflare configuration and dashboard limitation](https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/)

## Authorize and deploy

From the project root:

```powershell
.\tools\worker.ps1 -Action login
.\tools\worker.ps1 -Action whoami
.\tools\worker.ps1 -Action deploy
```

The login command opens Cloudflare's official OAuth authorization page. Complete it in the browser for the account containing `esp32-companion-control`. It requests only `account:read`, `user:read`, and `workers_scripts:write`; these allow account discovery and Worker script management. This permission is account-wide for Worker scripts, although the project wrapper restricts its deployment target name to `esp32-companion-control`. It does not request all of Wrangler's default scopes.

Wrangler is configured to use OS-keyring credential storage; on Windows it may install its pinned npm keyring helper. No account API token or OAuth credential needs to be pasted into chat or the project. [Official login/scopes/keyring options](https://developers.cloudflare.com/workers/wrangler/commands/general/#login)

The deployment uses the existing Worker name and Production device secret. The local private value is not embedded in its code or configuration. Confirm the account before deploying if Wrangler offers more than one. Keep the `phase-3b-hosted-auth-verified` checkpoint as the prior working version.

## Verify

```powershell
.\tools\worker.ps1 -Action test
.\tools\worker.ps1 -Action check
python .\tools\check_rate_limit.py --recover
```

The HTTP checker sends at most 35 authenticated burst requests to the auth-only route, looking for a 429. It verifies rejection/health behavior during throttling, then waits 61 seconds and checks successful recovery. It reads the ignored local credential, refuses redirects, uses HTTPS outside loopback, and logs only controlled result metadata. It makes no Gemini requests. Do not expect the hosted boundary to be exactly request 11 because Cloudflare's enforcement is approximate.

For local HTTP verification, start the development server and use a second terminal:

```powershell
.\tools\worker.ps1 -Action dev
python .\tools\check_rate_limit.py --base-url http://127.0.0.1:8787 --recover
```

Stop the development server with **x** afterwards. A completed burst test intentionally spends the current device allowance; wait its retry window before running the ordinary authentication checker again.

Local evidence: 11 runtime test groups passed; the real HTTP test admitted 10 requests, rejected the next with 429/Retry-After 60, kept health and credential rejection working, and accepted the recovery request after 61 seconds. The ordinary three-case authentication test also passed after recovery. See [TEST_LOG.md](TEST_LOG.md) for logs and hosted status.
