# Worker health checkpoint

Phase 3a is verified locally and on the hosted HTTPS address on 2026-11-17. The Worker is a small web service that will later authenticate the robot and issue Gemini session access. Its first route, `/health`, proves that the service runs and responds correctly.

The current health response means the Worker program is running. It does not test device authentication, Gemini availability, quotas or a voice conversation. Those have separate checks: hosted device authentication, shared request limits, temporary-token issuance and a desktop Gemini setup acknowledgment now pass. See [GEMINI_SESSIONS.md](GEMINI_SESSIONS.md) for the current stage.

## Verified dashboard deployment

The user created `esp32-companion-control` through **Create app → Start with Hello World! → Deploy**, then replaced the starter with our prepared handler. Its public health address is **https://esp32-companion-control.stiener.workers.dev/health**. The user confirms it works; independent HTTPS GET/HEAD, unsupported-method and unknown-route checks all passed on 2026-11-17.

That dashboard procedure established the initial health-only service. The current code also requires the configured Durable Object binding and Production Secrets, so publish it with Wrangler from the project root:

```powershell
.\tools\worker.ps1 -Action test
.\tools\worker.ps1 -Action check
.\tools\worker.ps1 -Action deploy
```

Wrangler is already authorized on this PC. The health handler uses the `/health` route; the bare `/` address returns `404` with `{"error":"not_found"}`. The local development server can stay stopped while using the hosted Worker.

## Try it on this PC

From the project root in PowerShell:

```powershell
.\tools\worker.ps1 -Action dev
```

Wait for `Ready on http://127.0.0.1:8787`, then open **http://127.0.0.1:8787/health** in a browser on this PC. Expected response:

```json
{"status":"ok","service":"esp32-companion","api_version":1}
```

`127.0.0.1` means this PC. The development server listens there while the command runs. Press **x** in its terminal (or Ctrl+C) to stop it. No robot interaction is needed for this check; its existing wake/listen/cue behavior continues.

Local health needs no Cloudflare login or Gemini API key. With the current configuration, Wrangler warns that `GEMINI_API_KEY` is missing locally; that is expected because the real key remains in Production. Health works, and a valid local session request fails closed with 503. Fixture tests cover issuance without a real key. The development server was stopped after verification, so run the command above before opening the URL.

## Repeatable checks

```powershell
.\tools\worker.ps1 -Action install
.\tools\worker.ps1 -Action test
.\tools\worker.ps1 -Action check
```

- `install` uses `npm ci` and the saved package lock to install the pinned development dependencies. This needs internet access when packages are not cached.
- `test` runs 20 health/authentication/quota/session test groups inside Cloudflare's local `workerd` runtime through Miniflare, with fixture credentials and a fake Gemini provider.
- `check` builds the Worker using `wrangler deploy --dry-run`. This prepares a local bundle without publishing it.

| Request | Expected result |
|---|---|
| `GET /health` | `200`, JSON shown above, `Cache-Control: no-store` |
| `HEAD /health` | `200`, matching headers, empty body |
| Other methods on `/health` | `405`, `Allow: GET, HEAD` |
| `POST /device/check` | Authentication and shared-quota check; [contract](DEVICE_AUTH.md) |
| `POST /device/session` | Authenticated temporary-token issuance; [contract](GEMINI_SESSIONS.md) |
| Other routes | `404` |

Health and device-check routes make no Gemini requests. Only an authenticated, admitted session request can contact Google's token service. The source contains no embedded credentials. The real Gemini key is stored only as a Production Worker Secret; fixture tests do not read or need it.

## Tooling and files

The separate `cloudflare-worker/` project contains:

- `src/index.js`: request handler.
- `wrangler.jsonc`: Worker name, entry point, compatibility date and local server configuration.
- `package.json` / `package-lock.json`: pinned development tools and commands.
- `test/health.test.js`: runtime checks for health, methods, missing routes, authentication, shared quota and Gemini session/error behavior.

`tools/worker.ps1` locates Node.js, selects this project and runs its commands. This PC has a project-local Node.js v24.22.3 distribution under ignored `.tools/`, downloaded from Node.js and verified against its published SHA256. Its download manifest is saved there. On another PC, install an active Node.js LTS release with npm (at least Node 22.12), reopen PowerShell and run the install command above.

Wrangler is pinned to 4.164.0; the test uses its matching Miniflare 5.20261117.0-beta and workerd 1.20261117.1. npm's install-script entries are limited to the pinned workerd/esbuild build tools. Generated dependencies, Wrangler output, local secret files, tool archives and logs are excluded from Git.

Cloudflare currently lists Windows 11 as the supported Windows platform for Wrangler. This PC runs Windows 10 Pro; the pinned runtime tests, bundle and local HTTP server all passed here. That verifies this setup, rather than establishing vendor support for future upgrades. Use a supported OS if a later runtime update stops working.

## Next checkpoint

1. **Complete:** publish the health-only Worker and verify its HTTPS URL.
2. **Complete:** store the device credential as a Production Secret and verify missing/wrong rejection plus correct-credential success over HTTPS.
3. **Complete:** shared request limits, retry responses and recovery are verified locally and online. Wrangler is authorized and deploys the binding/migration plus code together. See [RATE_LIMITS.md](RATE_LIMITS.md).
4. **Complete:** current model/token schema checked, permanent key saved as a Production Secret, hosted token issuance and a desktop Gemini setup acknowledgment verified. User reports a Free-tier project; remaining quota is not measured by this check.
5. **First spoken reply complete:** user confirms clear desktop speech at normal speed. Next verify a short prerecorded audio-input exchange before joining it to the ESP32. Finish microphone headroom calibration before voice streaming. [Desktop voice check](DESKTOP_VOICE.md).

The permanent Gemini key belongs in the server secret workflow. It is not needed in chat, firmware or the health test.

## Device credential: hosted authentication verified

A random 256-bit device credential has been generated in the Git-ignored `cloudflare-worker/.dev.vars` file. Its name is `DEVICE_AUTH_TOKEN`. The first value was exposed by an IDE text selection and was replaced locally without printing its replacement. The user has now confirmed saving the replacement as a Production Secret. This is the robot's shared credential, separate from a Gemini API key or a Cloudflare account/deployment token.

In the Worker's Settings, under **Runtime variables and secrets**, use **Add variable** with the Production tab selected:

1. Set Type to **Secret**.
2. Set Name to `DEVICE_AUTH_TOKEN`.
3. Open the local `.dev.vars` file and copy only the 64 characters inside the quotes into Value.
4. Save/deploy the change. Report only that the secret was saved; keep its value out of chat and screenshots.

The local and hosted authentication handler/checker pass correct/missing/wrong credential tests. The first hosted attempt found a mismatch; re-saving the current local value as the Production Secret resolved it. Public `/health` still passes. See [DEVICE_AUTH.md](DEVICE_AUTH.md). No ESP32 flash or Gemini session is involved in this increment.

Official references checked for this checkpoint: [Wrangler installation and requirements](https://developers.cloudflare.com/workers/wrangler/install-and-update/), [local development](https://developers.cloudflare.com/workers/local-development/), [Wrangler configuration](https://developers.cloudflare.com/workers/wrangler/configuration/), and [Miniflare testing](https://developers.cloudflare.com/workers/testing/miniflare/). Actual tests use the API shipped in the pinned packages. See [TEST_LOG.md](TEST_LOG.md) for the recorded results.
