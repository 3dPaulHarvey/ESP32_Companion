import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { after, test } from "node:test";
import { Miniflare } from "miniflare";

const config = JSON.parse(await readFile(new URL("../wrangler.jsonc", import.meta.url), "utf8"));
const source = await readFile(new URL(`../${config.main}`, import.meta.url), "utf8");
// Public test fixture, not the credential from .dev.vars.
const testToken = "0123456789abcdef".repeat(4);
function makeWorker(env = {}, withRateLimit = true, suffix = "") {
  const workerName = config.name + suffix;
  const rateEnv = withRateLimit ? {
    DEVICE_RATE_LIMITER: { type: "durable-object", worker: workerName, exportName: "DeviceRateLimiter" },
  } : {};
  return new Miniflare({
    workers: [{
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

test("unknown routes, including the unimplemented session route, return 404", async () => {
  for (const path of ["/", "/health/", "/device/session", "/not-found"]) {
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
