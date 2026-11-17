const health = {
  status: "ok",
  service: "esp32-companion",
  api_version: 1,
};

const encoder = new TextEncoder();

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

export default {
  fetch(request, env) {
    const { pathname } = new URL(request.url);
    const headers = { "Cache-Control": "no-store" };

    if (pathname === "/device/check") {
      if (request.method !== "POST") {
        return Response.json({ error: "method_not_allowed" }, {
          status: 405,
          headers: { ...headers, Allow: "POST" },
        });
      }
      const rejection = authenticateDevice(request, env, headers);
      if (rejection) return rejection;
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
