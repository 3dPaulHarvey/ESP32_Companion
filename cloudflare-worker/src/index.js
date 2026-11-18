import { DurableObject } from "cloudflare:workers";

const health = {
  status: "ok",
  service: "esp32-companion",
  api_version: 1,
};

const encoder = new TextEncoder();
const deviceRequestLimit = 10;
const deviceWindowMs = 60000;
// One authenticated development device. Never use its secret as a counter key.
const deviceRateKey = "esp32-companion-control:development-device:v1";

export class DeviceRateLimiter extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS admissions (id INTEGER PRIMARY KEY, admitted_at INTEGER NOT NULL)");
  }

  admit() {
    // One globally addressed object serializes admission decisions. Persist the
    // bounded timestamp set so an isolate restart cannot reset the allowance.
    const now = Date.now();
    return this.ctx.storage.transactionSync(() => {
      const sql = this.ctx.storage.sql;
      sql.exec("DELETE FROM admissions WHERE admitted_at <= ?", now - deviceWindowMs);
      const { used, oldest } = sql.exec("SELECT COUNT(*) AS used, MIN(admitted_at) AS oldest FROM admissions").one();
      if (used >= deviceRequestLimit) {
        const retryAfterSeconds = Math.max(1, Math.min(60, Math.ceil((oldest + deviceWindowMs - now) / 1000)));
        return { success: false, retryAfterSeconds };
      }
      sql.exec("INSERT INTO admissions (admitted_at) VALUES (?)", now);
      return { success: true };
    });
  }
}

function authenticateDevice(request, env, headers) {
  const expected = env?.DEVICE_AUTH_TOKEN;
  // Our generated device credential is exactly 32 random bytes encoded as hex.
  // A missing or malformed server secret must never authorize a request.
  if (typeof expected !== "string" || !/^[0-9a-f]{64}$/.test(expected)) {
    return Response.json({ error: "service_unavailable" }, { status: 503, headers });
  }

  const supplied = /^Bearer ([0-9a-f]{64})$/i.exec(request.headers.get("Authorization") || "");
  if (!supplied || !crypto.subtle.timingSafeEqual(
    encoder.encode(supplied[1]), encoder.encode(expected),
  )) {
    return Response.json({ error: "unauthorized" }, {
      status: 401,
      headers: { ...headers, "WWW-Authenticate": 'Bearer realm="esp32-companion"' },
    });
  }
  return null;
}

async function enforceDeviceRateLimit(env, headers) {
  const limiter = env?.DEVICE_RATE_LIMITER;
  if (!limiter || typeof limiter.getByName !== "function") {
    return Response.json({ error: "service_unavailable" }, { status: 503, headers });
  }
  try {
    const result = await limiter.getByName(deviceRateKey).admit();
    if (typeof result?.success !== "boolean") {
      return Response.json({ error: "service_unavailable" }, { status: 503, headers });
    }
    if (!result.success) {
      if (!Number.isInteger(result.retryAfterSeconds) || result.retryAfterSeconds < 1 || result.retryAfterSeconds > 60) {
        return Response.json({ error: "service_unavailable" }, { status: 503, headers });
      }
      return Response.json({ error: "rate_limited" }, {
        status: 429,
        headers: { ...headers, "Retry-After": String(result.retryAfterSeconds) },
      });
    }
  } catch {
    // A limiter outage must not allow unbounded provider calls later.
    return Response.json({ error: "service_unavailable" }, { status: 503, headers });
  }
  return null;
}

export default {
  async fetch(request, env) {
    const { pathname } = new URL(request.url);
    const headers = { "Cache-Control": "no-store", "X-Companion-Revision": "rate-limit-v3" };

    if (pathname === "/device/check") {
      if (request.method !== "POST") {
        return Response.json({ error: "method_not_allowed" }, {
          status: 405,
          headers: { ...headers, Allow: "POST" },
        });
      }
      const rejection = authenticateDevice(request, env, headers);
      if (rejection) return rejection;
      const throttled = await enforceDeviceRateLimit(env, headers);
      if (throttled) return throttled;
      return Response.json({ authenticated: true }, { headers });
    }

    if (pathname !== "/health") {
      return Response.json({ error: "not_found" }, { status: 404, headers });
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return Response.json({ error: "method_not_allowed" }, {
        status: 405,
        headers: { ...headers, Allow: "GET, HEAD" },
      });
    }

    return new Response(request.method === "HEAD" ? null : JSON.stringify(health), {
      headers: { ...headers, "Content-Type": "application/json; charset=utf-8" },
    });
  },
};
