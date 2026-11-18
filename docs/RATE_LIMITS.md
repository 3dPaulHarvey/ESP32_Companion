# Device request limits

Phase 3c permits **10 authenticated control requests in a rolling 60-second window for the one development device**, initially on `POST /device/check`. A shared SQLite-backed Durable Object keeps the allowance consistent across Cloudflare locations and persists it across isolate restarts. Voice audio will use a separate Gemini WebSocket, not this control endpoint.

Status on 2026-11-17: **deployed and verified locally and online**. Both HTTP tests admitted 10 requests, rejected the next, preserved health/authentication behavior during throttling, and accepted a request after the retry delay. All 12 runtime test groups pass, including concurrent admission tests. The serving revision is `rate-limit-v3`.

## Behavior

- Correct credentials are checked before the device quota. Missing/wrong credentials still receive 401 and do not spend that quota.
- An admitted request returns the existing `{"authenticated":true}` response.
- A throttled request returns **429**, `{"error":"rate_limited"}`, a `Retry-After` value from 1–60 seconds, and `Cache-Control: no-store`. The delay reflects when the oldest admission leaves the window; another caller may consume the next available slot.
- `/health` stays public and available when the device is throttled.
- Missing/invalid/unavailable limiter configuration rejects authenticated requests with 503. It does not fall back to an in-memory counter or unbounded access.
- One fixed, nonsecret counter key represents the one development device. Changing query parameters does not create another allowance. The device token is not used as the key or included in output.

The original built-in Rate Limiting API passed local tests but did not return 429 in bounded hosted bursts, even after confirming the deployed revision and binding. Its documented per-location, approximate behavior was insufficient for this checkpoint's verification, so the final implementation uses the shared transactional counter Cloudflare recommends for strict limits. The underlying reason for the permissive native responses was not independently established. [Cloudflare's strict-limit guidance](https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/#strict-limits)

This admission limit is still **not a billing cap**: rejected and health requests consume Worker invocations, and a future long Gemini conversation has its own usage. Verify provider quotas and handle provider 429 responses before adding sessions.

## Configuration

`cloudflare-worker/wrangler.jsonc` binds `DEVICE_RATE_LIMITER` to the exported `DeviceRateLimiter` class and declares the `device-rate-sqlite-v1` migration. The class performs its cleanup, count and admission insert in one synchronous SQLite transaction. It retains at most 10 admission timestamps and stores no credential, audio or conversation data. It has no timer or alarm.

The configuration requires the existing `DEVICE_AUTH_TOKEN` secret and preserves enabled observability and disabled preview URLs. Deploy code, binding and migration together using Wrangler. Pasting source alone into a fresh Worker would omit its required object binding. SQLite-backed Durable Objects are available on Workers Free, subject to its usage limits; no plan upgrade was performed. [Storage transactions](https://developers.cloudflare.com/durable-objects/api/sqlite-storage-api/#transactionsync), [Free-plan availability and limits](https://developers.cloudflare.com/durable-objects/platform/pricing/).

## Authorize and deploy

From the project root:

```powershell
.\tools\worker.ps1 -Action login
.\tools\worker.ps1 -Action whoami
.\tools\worker.ps1 -Action deploy
```

Login is already completed on this PC; repeat it when authorization expires or on another PC. The command opens Cloudflare's official OAuth page. It requests `account:read`, `user:read`, and `workers_scripts:write` for account discovery and Worker script management. This permission is account-wide for Worker scripts, although the project wrapper restricts its deployment target name to `esp32-companion-control`.

Wrangler is configured to use OS-keyring credential storage; on Windows it may install its pinned npm keyring helper. No account API token or OAuth credential needs to be pasted into chat or the project. [Official login/scopes/keyring options](https://developers.cloudflare.com/workers/wrangler/commands/general/#login)

The deployment uses the existing Worker name and Production device secret. The local private value is not embedded in its code or configuration. Confirm the account before deploying if Wrangler offers more than one. The final hosted version is `b3740e90-919a-44ab-a803-86472f19231a`; the health payload remains unchanged, with `X-Companion-Revision: rate-limit-v3` identifying the serving code.

## Verify

```powershell
.\tools\worker.ps1 -Action test
.\tools\worker.ps1 -Action check
python .\tools\check_rate_limit.py --recover
```

The HTTP checker sends at most 11 authenticated burst requests to the auth-only route. It requires the current revision marker and a 429 within that burst, verifies rejection/health behavior during throttling, then waits the remaining retry delay plus one second and checks recovery. It reads the ignored credential, refuses redirects, uses HTTPS outside loopback, and logs only controlled metadata. It makes no Gemini requests. A fresh allowance admits 10 requests; previous authenticated checks may have already spent some of it.

For local HTTP verification, start the development server and use a second terminal:

```powershell
.\tools\worker.ps1 -Action dev
python .\tools\check_rate_limit.py --base-url http://127.0.0.1:8787 --recover
```

Stop the development server with **x** afterwards. A completed burst test intentionally spends the current device allowance; wait its retry window before running the ordinary authentication checker again.

Evidence: 12 runtime test groups passed; a simultaneous 20-request test admitted exactly 10 and rejected 10. Both real local and hosted HTTP tests passed throttle/retry/recovery checks, followed by successful three-case authentication checks. See [TEST_LOG.md](TEST_LOG.md) for logs, deployment identity and the earlier native-limit results.
