# Local Worker health checkpoint

Phase 3a is verified locally on 2026-11-17. The Worker is a small web service that will later authenticate the robot and issue Gemini session access. Its first route, `/health`, proves that the service runs and responds correctly.

The current health response means the Worker program is running. It does not test device authentication, Gemini availability, quotas or a voice conversation. Cloud hosting and Gemini integration remain subsequent steps.

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

This local stage needs no Cloudflare login or Gemini API key. The development server was stopped after verification, so run the command above before opening the URL.

## Repeatable checks

```powershell
.\tools\worker.ps1 -Action install
.\tools\worker.ps1 -Action test
.\tools\worker.ps1 -Action check
```

- `install` uses `npm ci` and the saved package lock to install the pinned development dependencies. This needs internet access when packages are not cached.
- `test` runs four groups of HTTP contract checks inside Cloudflare's local `workerd` runtime through Miniflare.
- `check` builds the Worker using `wrangler deploy --dry-run`. This prepares a local bundle without publishing it.

| Request | Expected result |
|---|---|
| `GET /health` | `200`, JSON shown above, `Cache-Control: no-store` |
| `HEAD /health` | `200`, matching headers, empty body |
| Other methods on `/health` | `405`, `Allow: GET, HEAD` |
| Other routes, including `/device/session` | `404` |

The handler makes no Gemini or other outbound service requests. It contains no credentials. The service has no authentication/session endpoint yet.

## Tooling and files

The separate `cloudflare-worker/` project contains:

- `src/index.js`: request handler.
- `wrangler.jsonc`: Worker name, entry point, compatibility date and local server configuration.
- `package.json` / `package-lock.json`: pinned development tools and commands.
- `test/health.test.js`: runtime checks for health, HEAD, unsupported methods and missing routes.

`tools/worker.ps1` locates Node.js, selects this project and runs its commands. This PC has a project-local Node.js v24.22.3 distribution under ignored `.tools/`, downloaded from Node.js and verified against its published SHA256. Its download manifest is saved there. On another PC, install an active Node.js LTS release with npm (at least Node 22.12), reopen PowerShell and run the install command above.

Wrangler is pinned to 4.164.0; the test uses its matching Miniflare 5.20261117.0-beta and workerd 1.20261117.1. npm's install-script entries are limited to the pinned workerd/esbuild build tools. Generated dependencies, Wrangler output, local secret files, tool archives and logs are excluded from Git.

Cloudflare currently lists Windows 11 as the supported Windows platform for Wrangler. This PC runs Windows 10 Pro; the pinned runtime tests, bundle and local HTTP server all passed here. That verifies this setup, rather than establishing vendor support for future upgrades. Use a supported OS if a later runtime update stops working.

## Next checkpoint

1. Create or sign into a Cloudflare account, then publish this health-only Worker and verify its HTTPS URL.
2. Add device authentication and request limits; verify rejected requests before implementing session issuance.
3. Verify Gemini Live's current model/token support and quota, then store the permanent key as a Worker secret and test token issuance.
4. Prove a desktop Gemini connection before joining it to the ESP32. Finish microphone headroom calibration before voice streaming.

The permanent Gemini key belongs in the server secret workflow when we reach that step. It is not needed in chat, firmware or the health test.

Official references checked for this checkpoint: [Wrangler installation and requirements](https://developers.cloudflare.com/workers/wrangler/install-and-update/), [local development](https://developers.cloudflare.com/workers/local-development/), [Wrangler configuration](https://developers.cloudflare.com/workers/wrangler/configuration/), and [Miniflare testing](https://developers.cloudflare.com/workers/testing/miniflare/). Actual tests use the API shipped in the pinned packages. See [TEST_LOG.md](TEST_LOG.md) for the recorded results.
