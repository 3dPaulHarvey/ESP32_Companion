import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { after, test } from "node:test";
import { Miniflare, Response as MockResponse } from "miniflare";

const config = JSON.parse(await readFile(new URL("../wrangler.jsonc", import.meta.url), "utf8"));
const source = await readFile(new URL(`../${config.main}`, import.meta.url), "utf8");
// Public test fixture, not the credential from .dev.vars.
const testToken = "0123456789abcdef".repeat(4);
function makeWorker(env = {}, withRateLimit = true, suffix = "", provider = () => {
  throw new Error("Unexpected outbound request in a local test");
}) {
  const workerName = config.name + suffix;
  const rateEnv = withRateLimit ? {
    DEVICE_RATE_LIMITER: { type: "durable-object", worker: workerName, exportName: "DeviceRateLimiter" },
  } : {};
  return new Miniflare({
    workers: [{
      dev: { outboundService: { type: "fetcher", handler: provider } },
      config: {
        name: workerName,
        compatibilityDate: config.compatibility_date,
        env: { ...rateEnv, ...env },
        exports: { DeviceRateLimiter: { type: "durable-object", storage: "sqlite" } },
        manifest: {
          mainModule: "index.js",
          modules: { "index.js": { type: "esm", contents: source } },
        },
      },
    }],
    host: "127.0.0.1",
    port: 0,
    cf: false,
  });
}
const worker = makeWorker({ DEVICE_AUTH_TOKEN: { type: "json", value: testToken } });
const unconfiguredWorker = makeWorker();
const malformedConfigWorker = makeWorker({ DEVICE_AUTH_TOKEN: { type: "json", value: `"${testToken}"` } });
const noLimiterWorker = makeWorker({ DEVICE_AUTH_TOKEN: { type: "json", value: testToken } }, false);
const invalidLimiterWorker = makeWorker({
  DEVICE_AUTH_TOKEN: { type: "json", value: testToken },
  DEVICE_RATE_LIMITER: { type: "json", value: "invalid" },
});
const limitedWorker = makeWorker(
  { DEVICE_AUTH_TOKEN: { type: "json", value: testToken } }, true,
  "-burst",
);
const parallelWorker = makeWorker({ DEVICE_AUTH_TOKEN: { type: "json", value: testToken } }, true, "-parallel");
after(() => Promise.all([
  worker, unconfiguredWorker, malformedConfigWorker, noLimiterWorker, invalidLimiterWorker, limitedWorker, parallelWorker,
].map(instance => instance.dispose())));

test("GET /health returns service status as noncached JSON in workerd", async () => {
  const response = await worker.dispatchFetch("http://localhost/health");
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type"), /^application\/json/);
  assert.equal(response.headers.get("cache-control"), "no-store");
  assert.equal(response.headers.get("x-companion-revision"), "gemini-session-v1");
  assert.deepEqual(await response.json(), {
    status: "ok",
    service: "esp32-companion",
    api_version: 1,
  });
});

test("HEAD /health returns status and headers with an empty body", async () => {
  const response = await worker.dispatchFetch("http://localhost/health", { method: "HEAD" });
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type"), /^application\/json/);
  assert.equal(response.headers.get("cache-control"), "no-store");
  assert.equal(await response.text(), "");
});

test("unsupported methods on /health return 405 and advertise allowed methods", async () => {
  for (const method of ["POST", "PUT", "DELETE", "OPTIONS"]) {
    const response = await worker.dispatchFetch("http://localhost/health", { method });
    assert.equal(response.status, 405, method);
    assert.equal(response.headers.get("allow"), "GET, HEAD");
    assert.deepEqual(await response.json(), { error: "method_not_allowed" });
  }
});

test("unknown routes return 404", async () => {
  for (const path of ["/", "/health/", "/not-found"]) {
    for (const method of ["GET", "POST"]) {
      const response = await worker.dispatchFetch(`http://localhost${path}`, { method });
      assert.equal(response.status, 404, `${method} ${path}`);
      assert.deepEqual(await response.json(), { error: "not_found" });
    }
  }
});

test("POST /device/check accepts the correct bearer token without returning it", async () => {
  for (const scheme of ["Bearer", "bearer"]) {
    const response = await worker.dispatchFetch("http://localhost/device/check", {
      method: "POST",
      headers: { Authorization: `${scheme} ${testToken}` },
    });
    assert.equal(response.status, 200);
    assert.equal(response.headers.get("cache-control"), "no-store");
    assert.deepEqual(await response.json(), { authenticated: true });
  }
});

test("missing, incorrect and malformed credentials all get the same 401", async () => {
  for (const authorization of [
    null,
    `Bearer f${testToken.slice(1)}`,
    `Bearer ${testToken.slice(0, -1)}0`,
    `Bearer ${testToken.slice(1)}`,
    `Bearer ${testToken}0`,
    `Basic ${testToken}`,
    `Bearer ${testToken}, Bearer ${testToken}`,
    `Bearer ${testToken.toUpperCase()}`,
    "Bearer invalid",
  ]) {
    const response = await worker.dispatchFetch("http://localhost/device/check", {
      method: "POST",
      headers: authorization ? { Authorization: authorization } : {},
    });
    assert.equal(response.status, 401);
    assert.equal(response.headers.get("cache-control"), "no-store");
    assert.equal(response.headers.get("www-authenticate"), 'Bearer realm="esp32-companion"');
    assert.deepEqual(await response.json(), { error: "unauthorized" });
  }
});

test("a token in a query string or request body does not authenticate", async () => {
  for (const path of ["/device/check", `/device/check?token=${testToken}`]) {
    const response = await worker.dispatchFetch(`http://localhost${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ token: testToken }),
    });
    assert.equal(response.status, 401);
    assert.deepEqual(await response.json(), { error: "unauthorized" });
  }
});

test("missing or malformed server configuration fails closed, with health still available", async () => {
  for (const instance of [unconfiguredWorker, malformedConfigWorker]) {
    const response = await instance.dispatchFetch("http://localhost/device/check", {
      method: "POST",
      headers: { Authorization: `Bearer ${testToken}` },
    });
    assert.equal(response.status, 503);
    assert.equal(response.headers.get("cache-control"), "no-store");
    assert.deepEqual(await response.json(), { error: "service_unavailable" });
    const healthResponse = await instance.dispatchFetch("http://localhost/health");
    assert.equal(healthResponse.status, 200);
    await healthResponse.arrayBuffer();
  }
});

test("the device check requires POST and advertises that method", async () => {
  for (const method of ["GET", "HEAD", "PUT", "DELETE", "OPTIONS"]) {
    const response = await worker.dispatchFetch("http://localhost/device/check", { method });
    assert.equal(response.status, 405, method);
    assert.equal(response.headers.get("allow"), "POST");
    assert.equal(response.headers.get("cache-control"), "no-store");
    if (method === "HEAD") assert.equal(await response.text(), "");
    else assert.deepEqual(await response.json(), { error: "method_not_allowed" });
  }
});

test("a missing or invalid limiter fails closed after authentication while health stays public", async () => {
  for (const instance of [noLimiterWorker, invalidLimiterWorker]) {
    const response = await instance.dispatchFetch("http://localhost/device/check", {
      method: "POST", headers: { Authorization: `Bearer ${testToken}` },
    });
    assert.equal(response.status, 503);
    assert.equal(response.headers.get("cache-control"), "no-store");
    assert.deepEqual(await response.json(), { error: "service_unavailable" });
    const unauthorized = await instance.dispatchFetch("http://localhost/device/check", { method: "POST" });
    assert.equal(unauthorized.status, 401);
    await unauthorized.arrayBuffer();
    const healthy = await instance.dispatchFetch("http://localhost/health");
    assert.equal(healthy.status, 200);
    await healthy.arrayBuffer();
  }
});

test("device quota ignores bad credentials, enforces 429, and leaves health available", async () => {
  for (let i = 0; i < 12; ++i) {
    const response = await limitedWorker.dispatchFetch("http://localhost/device/check", {
      method: "POST", headers: { Authorization: `Bearer f${testToken.slice(1)}` },
    });
    assert.equal(response.status, 401);
    await response.arrayBuffer();
  }
  for (let i = 0; i < 10; ++i) {
    const response = await limitedWorker.dispatchFetch(`http://localhost/device/check?attempt=${i}`, {
      method: "POST", headers: { Authorization: `Bearer ${testToken}` },
    });
    assert.equal(response.status, 200);
    assert.deepEqual(await response.json(), { authenticated: true });
  }
  for (let i = 0; i < 2; ++i) {
    const response = await limitedWorker.dispatchFetch(`http://localhost/device/check?different=${i}`, {
      method: "POST", headers: { Authorization: `Bearer ${testToken}` },
    });
    assert.equal(response.status, 429);
    assert.ok(/^[1-9][0-9]?$/.test(response.headers.get("retry-after")));
    assert.ok(Number(response.headers.get("retry-after")) <= 60);
    assert.equal(response.headers.get("cache-control"), "no-store");
    assert.deepEqual(await response.json(), { error: "rate_limited" });
  }
  const unauthorized = await limitedWorker.dispatchFetch("http://localhost/device/check", { method: "POST" });
  assert.equal(unauthorized.status, 401);
  await unauthorized.arrayBuffer();
  const healthy = await limitedWorker.dispatchFetch("http://localhost/health");
  assert.equal(healthy.status, 200);
  await healthy.arrayBuffer();
});

test("parallel authenticated requests share one atomic admission budget", async () => {
  const responses = await Promise.all(Array.from({ length: 20 }, (_, i) =>
    parallelWorker.dispatchFetch(`http://localhost/device/check?parallel=${i}`, {
      method: "POST", headers: { Authorization: `Bearer ${testToken}` },
    })));
  assert.equal(responses.filter(response => response.status === 200).length, 10);
  assert.equal(responses.filter(response => response.status === 429).length, 10);
  for (const response of responses) await response.arrayBuffer();
});

const fixtureKey = "fixture-gemini-api-key-never-valid";
const fixtureTemporaryToken = "auth_tokens/public-test-fixture-not-a-credential";
const authHeaders = { Authorization: `Bearer ${testToken}` };

function sessionWorker(t, provider, env = {}, withRateLimit = true) {
  const instance = makeWorker({
    DEVICE_AUTH_TOKEN: { type: "json", value: testToken },
    GEMINI_API_KEY: { type: "json", value: fixtureKey },
    ...env,
  }, withRateLimit, "-session", provider);
  t.after(() => instance.dispose());
  return instance;
}

async function assertSessionError(response, status, error) {
  assert.equal(response.status, status);
  assert.equal(response.headers.get("cache-control"), "no-store");
  assert.deepEqual(await response.json(), { error });
}

test("session setup requires POST and authentication before any provider call", async t => {
  let calls = 0;
  const instance = sessionWorker(t, () => { calls++; throw new Error("Must not call provider"); });
  for (const method of ["GET", "HEAD", "PUT", "OPTIONS"]) {
    const response = await instance.dispatchFetch("http://localhost/device/session", { method });
    assert.equal(response.status, 405);
    assert.equal(response.headers.get("allow"), "POST");
    await response.arrayBuffer();
  }
  for (const headers of [{}, { Authorization: `Bearer f${testToken.slice(1)}` }]) {
    const response = await instance.dispatchFetch("http://localhost/device/session", { method: "POST", headers });
    assert.equal(response.headers.get("www-authenticate"), 'Bearer realm="esp32-companion"');
    await assertSessionError(response, 401, "unauthorized");
  }
  assert.equal(calls, 0);
});

test("missing provider key or limiter fails closed without an outbound request", async t => {
  let calls = 0;
  const provider = () => { calls++; throw new Error("Must not call provider"); };
  for (const value of [null, "", "short", ` ${fixtureKey}`]) {
    const instance = sessionWorker(t, provider, { GEMINI_API_KEY: { type: "json", value } });
    await assertSessionError(await instance.dispatchFetch("http://localhost/device/session", {
      method: "POST", headers: authHeaders,
    }), 503, "service_unavailable");
  }
  const noLimiter = sessionWorker(t, provider, {}, false);
  await assertSessionError(await noLimiter.dispatchFetch("http://localhost/device/session", {
    method: "POST", headers: authHeaders,
  }), 503, "service_unavailable");
  assert.equal(calls, 0);
});

test("one-use five-minute token locks the full REST setup and returns only approved fields", async t => {
  const before = Date.now();
  let issued;
  const instance = sessionWorker(t, async request => {
    assert.equal(request.url, "https://generativelanguage.googleapis.com/v1beta/auth_tokens");
    assert.equal(request.method, "POST");
    assert.equal(request.headers.get("x-goog-api-key"), fixtureKey);
    assert.equal(request.headers.get("authorization"), null);
    issued = await request.json();
    return MockResponse.json({ name: fixtureTemporaryToken, secretExtra: fixtureKey, uses: 0 });
  });
  const response = await instance.dispatchFetch("http://localhost/device/session?model=ignored", {
    method: "POST", headers: authHeaders, body: JSON.stringify({ model: "another-model", uses: 0, tools: ["ignored"] }),
  });
  assert.equal(response.status, 200);
  assert.equal(response.headers.get("cache-control"), "no-store");
  const result = await response.json();
  assert.deepEqual(Object.keys(result).sort(), ["token", "model", "expires_at", "new_session_expires_at", "websocket_url"].sort());
  assert.equal(result.token, fixtureTemporaryToken);
  assert.equal(result.model, "models/gemini-3.8-live");
  assert.equal(result.websocket_url, "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContentConstrained");
  assert.equal(new URL(result.websocket_url).search, "");
  assert.equal(result.expires_at, issued.expireTime);
  assert.equal(result.new_session_expires_at, issued.newSessionExpireTime);
  assert.equal(Date.parse(issued.expireTime) - Date.parse(issued.newSessionExpireTime), 240000);
  assert.ok(Date.parse(issued.newSessionExpireTime) >= before + 60000);
  assert.ok(Date.parse(issued.newSessionExpireTime) <= Date.now() + 60000);
  assert.equal(issued.uses, 1);
  assert.equal(issued.fieldMask, undefined);
  assert.equal(issued.liveConnectConstraints, undefined);
  assert.deepEqual(issued.bidiGenerateContentSetup, {
    model: "models/gemini-3.8-live",
    generationConfig: { responseModalities: ["AUDIO"], maxOutputTokens: 256 },
    systemInstruction: { parts: [{ text: "You are a friendly robot companion. Speak English and keep replies to one or two short sentences." }] },
    outputAudioTranscription: {},
  });
  assert.ok(!JSON.stringify(result).includes(fixtureKey));
});

test("session issuance and device checks share the same ten-request budget", async t => {
  let calls = 0;
  const instance = sessionWorker(t, () => {
    calls++;
    return MockResponse.json({ name: fixtureTemporaryToken });
  });
  for (let i = 0; i < 10; ++i) {
    const path = i % 2 ? "/device/session" : "/device/check";
    const response = await instance.dispatchFetch(`http://localhost${path}`, { method: "POST", headers: authHeaders });
    assert.equal(response.status, 200);
    await response.arrayBuffer();
  }
  for (const path of ["/device/session", "/device/check"]) {
    const response = await instance.dispatchFetch(`http://localhost${path}`, { method: "POST", headers: authHeaders });
    assert.ok(Number(response.headers.get("retry-after")) >= 1);
    await assertSessionError(response, 429, "rate_limited");
  }
  assert.equal(calls, 5);
});

test("provider rejection and quota failures expose no provider body or key", async t => {
  let providerStatus;
  const instance = sessionWorker(t, () => new MockResponse(fixtureKey, { status: providerStatus }));
  for (providerStatus of [400, 401, 403, 429, 500]) {
    const response = await instance.dispatchFetch("http://localhost/device/session", { method: "POST", headers: authHeaders });
    assert.equal(response.headers.get("x-companion-provider-status"), String(providerStatus));
    if (providerStatus === 429) {
      assert.equal(response.headers.get("retry-after"), "60");
      await assertSessionError(response, 429, "provider_rate_limited");
    } else {
      await assertSessionError(response, 502, "provider_unavailable");
    }
  }
});

test("bad, oversized and credential-echoing provider responses fail closed", async t => {
  let providerResponse;
  const instance = sessionWorker(t, () => providerResponse);
  const cases = [
    new MockResponse("not JSON", { headers: { "Content-Type": "application/json" } }),
    new MockResponse(JSON.stringify({ name: fixtureTemporaryToken })),
    MockResponse.json(null),
    MockResponse.json({}),
    MockResponse.json({ name: "too-short" }),
    MockResponse.json({ name: "invalid token with whitespace" }),
    MockResponse.json({ name: fixtureKey }),
    MockResponse.json({ name: "x".repeat(8193) }),
    MockResponse.json({ name: fixtureTemporaryToken, huge: "x".repeat(16385) }),
  ];
  for (providerResponse of cases) {
    await assertSessionError(await instance.dispatchFetch("http://localhost/device/session", {
      method: "POST", headers: authHeaders,
    }), 502, "provider_unavailable");
  }
});

test("provider redirects cannot forward the API key to another origin", async t => {
  let calls = 0;
  const instance = sessionWorker(t, request => {
    calls++;
    assert.equal(new URL(request.url).origin, "https://generativelanguage.googleapis.com");
    return new MockResponse(null, { status: 302, headers: { Location: "https://example.org/never-follow" } });
  });
  await assertSessionError(await instance.dispatchFetch("http://localhost/device/session", {
    method: "POST", headers: authHeaders,
  }), 502, "provider_unavailable");
  assert.equal(calls, 1);
});

test("an unresponsive provider times out without returning its error details", { timeout: 15000 }, async t => {
  const instance = sessionWorker(t, async () => {
    await new Promise(resolve => setTimeout(resolve, 11000));
    return MockResponse.json({ name: fixtureTemporaryToken });
  });
  const started = Date.now();
  await assertSessionError(await instance.dispatchFetch("http://localhost/device/session", {
    method: "POST", headers: authHeaders,
  }), 504, "provider_timeout");
  assert.ok(Date.now() - started < 13000);
});
