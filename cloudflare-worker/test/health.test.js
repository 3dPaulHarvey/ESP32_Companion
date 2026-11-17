import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { after, test } from "node:test";
import { Miniflare } from "miniflare";

const config = JSON.parse(await readFile(new URL("../wrangler.jsonc", import.meta.url), "utf8"));
const source = await readFile(new URL(`../${config.main}`, import.meta.url), "utf8");
const worker = new Miniflare({
  workers: [{
    config: {
      name: config.name,
      compatibilityDate: config.compatibility_date,
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
after(() => worker.dispose());

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
