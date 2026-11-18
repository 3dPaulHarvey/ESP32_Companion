const health = {
  status: "ok",
  service: "esp32-companion",
  api_version: 1,
};

const encoder = new TextEncoder();
const retryAfterSeconds = 60;
// One authenticated development device. Never use its secret as a counter key.
const deviceRateKey = "esp32-companion-control:development-device:v1";

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
  if (!limiter || typeof limiter.limit !== "function") {
    return Response.json({ error: "service_unavailable" }, { status: 503, headers });
  }
  try {
    const result = await limiter.limit({ key: deviceRateKey, delta: 1 });
    if (typeof result?.success !== "boolean") {
      return Response.json({ error: "service_unavailable" }, { status: 503, headers });
    }
    if (!result.success) {
      return Response.json({ error: "rate_limited" }, {
        status: 429,
        headers: { ...headers, "Retry-After": String(retryAfterSeconds) },
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
    const headers = { "Cache-Control": "no-store", "X-Companion-Revision": "rate-limit-v2" };

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
