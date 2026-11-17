# Worker health checkpoint

Phase 3a is verified locally and on the hosted HTTPS address on 2026-11-17. The Worker is a small web service that will later authenticate the robot and issue Gemini session access. Its first route, `/health`, proves that the service runs and responds correctly.

The current health response means the Worker program is running. It does not test device authentication, Gemini availability, quotas or a voice conversation. Device authentication and Gemini integration remain subsequent steps.

## Verified dashboard deployment

The user created `esp32-companion-control` through **Create app → Start with Hello World! → Deploy**, then replaced the starter with our prepared handler. Its public health address is **https://esp32-companion-control.stiener.workers.dev/health**. The user confirms it works; independent HTTPS GET/HEAD, unsupported-method and unknown-route checks all passed on 2026-11-17.

To publish the prepared handler from the dashboard editor:

1. Open this project's `cloudflare-worker/src/index.js` in VS Code. Select all and copy its contents.
2. Click inside the Cloudflare editor's `worker.js` code area. Select all and paste, replacing the entire starter program.
3. Click **Deploy** at the top right.
4. Open **https://esp32-companion-control.stiener.workers.dev/health** in a browser tab. Expect `{"status":"ok","service":"esp32-companion","api_version":1}`.

The health handler uses the `/health` route. The bare `/` address returns `404` with `{"error":"not_found"}` as designed. The dashboard filename `worker.js` may differ from our local `src/index.js`; use the same program contents. No Wrangler account authorization is configured by this manual dashboard workflow. The local Node development server can stay stopped; the hosted Worker runs independently.

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
- `test` runs health and device-authentication HTTP contract checks inside Cloudflare's local `workerd` runtime through Miniflare.
- `check` builds the Worker using `wrangler deploy --dry-run`. This prepares a local bundle without publishing it.

| Request | Expected result |
|---|---|
| `GET /health` | `200`, JSON shown above, `Cache-Control: no-store` |
| `HEAD /health` | `200`, matching headers, empty body |
| Other methods on `/health` | `405`, `Allow: GET, HEAD` |
| Other routes, including `/device/session` | `404` |

The handler makes no Gemini or other outbound service requests. It contains no embedded credentials. The hosted health-only version is verified; a local device-authentication check is now prepared and tested. See [DEVICE_AUTH.md](DEVICE_AUTH.md) to publish and verify that next increment. Gemini session issuance remains unimplemented.

## Tooling and files

The separate `cloudflare-worker/` project contains:

- `src/index.js`: request handler.
- `wrangler.jsonc`: Worker name, entry point, compatibility date and local server configuration.
- `package.json` / `package-lock.json`: pinned development tools and commands.
- `test/health.test.js`: runtime checks for health, allowed methods, missing routes and device authentication.

`tools/worker.ps1` locates Node.js, selects this project and runs its commands. This PC has a project-local Node.js v24.22.3 distribution under ignored `.tools/`, downloaded from Node.js and verified against its published SHA256. Its download manifest is saved there. On another PC, install an active Node.js LTS release with npm (at least Node 22.12), reopen PowerShell and run the install command above.

Wrangler is pinned to 4.164.0; the test uses its matching Miniflare 5.20261117.0-beta and workerd 1.20261117.1. npm's install-script entries are limited to the pinned workerd/esbuild build tools. Generated dependencies, Wrangler output, local secret files, tool archives and logs are excluded from Git.

Cloudflare currently lists Windows 11 as the supported Windows platform for Wrangler. This PC runs Windows 10 Pro; the pinned runtime tests, bundle and local HTTP server all passed here. That verifies this setup, rather than establishing vendor support for future upgrades. Use a supported OS if a later runtime update stops working.

## Next checkpoint

1. **Complete:** publish the health-only Worker and verify its HTTPS URL.
2. Add a device credential as an encrypted Worker secret, then implement a small authentication check. Missing/wrong credentials must be rejected, and the correct credential must succeed before adding session issuance or Gemini. Add request limits before enabling provider calls.
3. Verify Gemini Live's current model/token support and quota, then store the permanent key as a Worker secret and test token issuance.
4. Prove a desktop Gemini connection before joining it to the ESP32. Finish microphone headroom calibration before voice streaming.

The permanent Gemini key belongs in the server secret workflow when we reach that step. It is not needed in chat, firmware or the health test.

## Device credential: hosted save confirmed, authentication deployment pending

A random 256-bit device credential has been generated in the Git-ignored `cloudflare-worker/.dev.vars` file. Its name is `DEVICE_AUTH_TOKEN`. The first value was exposed by an IDE text selection and was replaced locally without printing its replacement. The user has now confirmed saving the replacement as a Production Secret. This is the robot's shared credential, separate from a Gemini API key or a Cloudflare account/deployment token.

In the Worker's Settings, under **Runtime variables and secrets**, use **Add variable** with the Production tab selected:

1. Set Type to **Secret**.
2. Set Name to `DEVICE_AUTH_TOKEN`.
3. Open the local `.dev.vars` file and copy only the 64 characters inside the quotes into Value.
4. Save/deploy the change. Report only that the secret was saved; keep its value out of chat and screenshots.

The local authentication handler and checker now pass correct/missing/wrong credential tests. Its hosted deployment is the next manual step; see [DEVICE_AUTH.md](DEVICE_AUTH.md). Saving the secret alone does not enforce authentication in the older health-only handler. No ESP32 flash or Gemini session is involved in this increment.

Official references checked for this checkpoint: [Wrangler installation and requirements](https://developers.cloudflare.com/workers/wrangler/install-and-update/), [local development](https://developers.cloudflare.com/workers/local-development/), [Wrangler configuration](https://developers.cloudflare.com/workers/wrangler/configuration/), and [Miniflare testing](https://developers.cloudflare.com/workers/testing/miniflare/). Actual tests use the API shipped in the pinned packages. See [TEST_LOG.md](TEST_LOG.md) for the recorded results.
