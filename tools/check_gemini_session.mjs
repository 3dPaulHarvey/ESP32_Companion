// The real API key stays in Cloudflare. This script holds only the device
// credential and the short-lived Gemini token, and logs neither one.
import { readFile, mkdir, writeFile } from "node:fs/promises";
import { parseArgs } from "node:util";

const hostedBase = "https://esp32-companion-control.stiener.workers.dev";
const model = "models/gemini-3.8-live";
const websocketUrl = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContentConstrained";
const root = new URL("../", import.meta.url);
const checks = [];
class CheckFailure extends Error {}

function pass(label, metadata = {}) {
  checks.push({ case: label, passed: true, ...metadata });
  console.log(`PASS ${label}`);
}

function baseOrigin(value) {
  let url;
  try { url = new URL(value); } catch { throw new CheckFailure("Use an HTTPS service origin or loopback HTTP."); }
  const loopback = ["localhost", "127.0.0.1", "[::1]"].includes(url.hostname);
  if (url.username || url.password || url.search || url.hash || url.pathname !== "/" ||
      (url.protocol !== "https:" && !(url.protocol === "http:" && loopback))) {
    throw new CheckFailure("Use an HTTPS service origin or loopback HTTP.");
  }
  return url.origin;
}

async function request(base, method, credential) {
  const headers = { "User-Agent": "ESP32-Companion-Session-Check/0.1" };
  if (credential) headers.Authorization = `Bearer ${credential}`;
  let response;
  let payload;
  try {
    response = await fetch(`${base}/device/session`, {
      method, headers, redirect: "manual", signal: AbortSignal.timeout(15000),
    });
    const reader = response.body?.getReader();
    const chunks = [];
    let size = 0;
    if (reader) {
      try {
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          size += value.byteLength;
          if (size > 16384) {
            await reader.cancel();
            throw new CheckFailure("Oversized reply");
          }
          chunks.push(value);
        }
      } finally { reader.releaseLock(); }
    }
    payload = JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    // A native network exception can contain credential-bearing URLs later.
    throw new CheckFailure("Session HTTP request failed or returned an invalid response.");
  }
  if (response.headers.get("cache-control") !== "no-store" ||
      !response.headers.get("content-type")?.startsWith("application/json") ||
      response.headers.get("x-companion-revision") !== "gemini-session-v1") {
    throw new CheckFailure("The expected noncached session endpoint is not serving yet.");
  }
  return { response, payload };
}

function expectRejection(result, status, error) {
  if (result.response.status !== status || JSON.stringify(result.payload) !== JSON.stringify({ error })) {
    throw new CheckFailure(`Expected the session rejection HTTP ${status}; received HTTP ${result.response.status}.`);
  }
  if (status === 401 && result.response.headers.get("www-authenticate") !== 'Bearer realm="esp32-companion"') {
    throw new CheckFailure("The session authentication challenge is missing.");
  }
}

async function connect(token) {
  // Gemini's constrained WebSocket protocol requires the ephemeral credential
  // as access_token. Construct the URL only in memory; never print it/errors.
  const url = new URL(websocketUrl);
  url.searchParams.set("access_token", token);
  await new Promise((resolve, reject) => {
    const socket = new WebSocket(url);
    let settled = false;
    function finish(error) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      try { socket.close(1000); } catch { /* Do not surface native errors. */ }
      if (error) reject(new CheckFailure(error));
      else resolve();
    }
    const timer = setTimeout(() => finish("Gemini setup did not complete within 15 seconds."), 15000);
    socket.addEventListener("open", () => {
      // The token locks the full policy. Send the matching model as the first
      // and only client message; no prompt, personal data or audio is sent.
      try { socket.send(JSON.stringify({ setup: { model } })); }
      catch { finish("Gemini closed before the setup message could be sent."); }
    });
    socket.addEventListener("message", async event => {
      if (settled) return;
      try {
        const raw = typeof event.data === "string" ? event.data : await event.data.text();
        if (raw.length > 16384) return finish("Unexpected oversized Gemini setup reply.");
        const message = JSON.parse(raw);
        if (Object.hasOwn(message, "setupComplete")) finish();
        else if (Object.hasOwn(message, "error")) finish("Google rejected the constrained Gemini setup; check project access and quota.");
      } catch {
        finish("Gemini returned an invalid setup response.");
      }
    });
    socket.addEventListener("error", () => finish("Gemini WebSocket connection failed; check project access and network."));
    socket.addEventListener("close", event => {
      if (!settled) finish(`Gemini closed before setup completed (code ${Number(event.code)}).`);
    });
  });
}

async function main() {
  let values;
  try {
    ({ values } = parseArgs({ options: {
      "base-url": { type: "string", default: hostedBase },
      name: { type: "string", default: "worker-gemini-session-check" },
      connect: { type: "boolean", default: false },
    } }));
  } catch { throw new CheckFailure("Use only --base-url ORIGIN, --name BASENAME and --connect."); }
  if (!/^[A-Za-z0-9_-]+$/.test(values.name)) throw new CheckFailure("Use a simple letters/digits/hyphens/underscores log name.");
  const base = baseOrigin(values["base-url"]);
  let privateText;
  try { privateText = await readFile(new URL("cloudflare-worker/.dev.vars", root), "utf8"); }
  catch { throw new CheckFailure("The local device credential file is unavailable."); }
  const matches = [...privateText.matchAll(/^DEVICE_AUTH_TOKEN="([0-9a-f]{64})"[ \t]*\r?$/gm)];
  privateText = undefined;
  if (matches.length !== 1) throw new CheckFailure("Expected one valid local device credential.");
  const credential = matches[0][1];
  const wrong = (credential[0] === "0" ? "1" : "0") + credential.slice(1);

  const method = await request(base, "GET");
  expectRejection(method, 405, "method_not_allowed");
  if (method.response.headers.get("allow") !== "POST") throw new CheckFailure("The session method contract is incorrect.");
  pass("session requires POST", { status: 405 });
  for (const [label, value] of [["missing credential rejected", undefined], ["incorrect credential rejected", wrong]]) {
    expectRejection(await request(base, "POST", value), 401, "unauthorized");
    pass(label, { status: 401 });
  }
  const { response, payload } = await request(base, "POST", credential);
  if (response.status !== 200) {
    const allowedErrors = ["rate_limited", "provider_rate_limited", "provider_unavailable", "provider_timeout", "service_unavailable"];
    const code = allowedErrors.includes(payload?.error) ? payload.error : "unexpected_response";
    const upstream = response.headers.get("x-companion-provider-status");
    const safeStatus = /^\d{3}$/.test(upstream || "") ? ` Google HTTP ${upstream}.` : "";
    throw new CheckFailure(`Session issuance returned HTTP ${response.status} (${code}).${safeStatus}`);
  }
  const fields = ["token", "model", "expires_at", "new_session_expires_at", "websocket_url"].sort();
  if (!payload || JSON.stringify(Object.keys(payload).sort()) !== JSON.stringify(fields) ||
      typeof payload.token !== "string" || payload.token.length < 16 || payload.token.length > 8192 ||
      !/^[\x21-\x7e]+$/.test(payload.token) || payload.model !== model || payload.websocket_url !== websocketUrl) {
    throw new CheckFailure("The temporary-token response does not match the expected contract.");
  }
  const remaining = Date.parse(payload.expires_at) - Date.now();
  const newRemaining = Date.parse(payload.new_session_expires_at) - Date.now();
  if (!Number.isFinite(remaining) || !Number.isFinite(newRemaining) ||
      remaining <= 0 || remaining > 305000 || newRemaining <= 0 || newRemaining > 65000 ||
      Date.parse(payload.expires_at) - Date.parse(payload.new_session_expires_at) !== 240000) {
    throw new CheckFailure("The temporary-token expiry window is invalid or elapsed.");
  }
  pass("temporary token issued with expected model and expiry", { status: 200, model });
  if (values.connect) {
    await connect(payload.token);
    pass("Gemini accepted the constrained WebSocket setup", { microphone_audio_sent: false, prompt_sent: false });
  }
  payload.token = undefined;
  const folder = new URL("logs/", root);
  await mkdir(folder, { recursive: true });
  await writeFile(new URL(`${values.name}.log`, folder), JSON.stringify({
    base_url: base, checked_at: new Date().toISOString(), checks,
  }, null, 2) + "\n");
  console.log("Session checks passed. No credential or temporary token was printed or logged.");
}

main().catch(error => {
  // Only our controlled messages reach the terminal; never print native stacks.
  const safe = error instanceof CheckFailure ? error.message : "Unexpected checker failure.";
  console.error(`Session check failed: ${safe}`);
  process.exitCode = 1;
});
