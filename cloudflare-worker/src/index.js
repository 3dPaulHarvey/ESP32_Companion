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
const geminiTokenUrl = "https://generativelanguage.googleapis.com/v1beta/auth_tokens";
const geminiWebSocketUrl = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContentConstrained";
const geminiModel = "models/gemini-3.8-live";
const providerTimeoutMs = 10000;

async function readProviderJson(response) {
  if (!response.headers.get("Content-Type")?.toLowerCase().startsWith("application/json") || !response.body) {
    throw new Error("Invalid provider response");
  }
  const reader = response.body.getReader();
  const chunks = [];
  let size = 0;
  try {
    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > 16384) {
        await reader.cancel();
        throw new Error("Provider response too large");
      }
      chunks.push(value);
    }
  } finally {
    reader.releaseLock();
  }
  const bytes = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
}

async function createGeminiSession(env, headers) {
  const key = env?.GEMINI_API_KEY;
  if (typeof key !== "string" || key.length < 20 || key.length > 4096 || /\s/.test(key)) {
    return Response.json({ error: "service_unavailable" }, { status: 503, headers });
  }
  const now = Date.now();
  const expiresAt = new Date(now + 5 * 60000).toISOString();
  const newSessionExpiresAt = new Date(now + 60000).toISOString();
  const signal = AbortSignal.timeout(providerTimeoutMs);
  try {
    const response = await fetch(geminiTokenUrl, {
      method: "POST",
      redirect: "manual",
      signal,
      headers: { "Content-Type": "application/json", "x-goog-api-key": key },
      body: JSON.stringify({
        uses: 1,
        expireTime: expiresAt,
        newSessionExpireTime: newSessionExpiresAt,
        // The REST schema calls this bidiGenerateContentSetup. Omitting a
        // fieldMask locks the entire setup, including the absence of tools.
        bidiGenerateContentSetup: {
          model: geminiModel,
          generationConfig: { responseModalities: ["AUDIO"], maxOutputTokens: 256 },
          systemInstruction: {
            parts: [{ text: "You are a friendly robot companion. Speak English and keep replies to one or two short sentences." }],
          },
          outputAudioTranscription: {},
        },
      }),
    });
    if (!response.ok) {
      await response.body?.cancel();
      const errorHeaders = { ...headers, "X-Companion-Provider-Status": String(response.status) };
      if (response.status === 429) {
        return Response.json({ error: "provider_rate_limited" }, {
          status: 429, headers: { ...errorHeaders, "Retry-After": "60" },
        });
      }
      return Response.json({ error: "provider_unavailable" }, { status: 502, headers: errorHeaders });
    }
    const result = await readProviderJson(response);
    const token = result?.name;
    if (typeof token !== "string" || token.length < 16 || token.length > 8192 ||
        !/^[\x21-\x7e]+$/.test(token) || token.includes(key)) {
      throw new Error("Invalid temporary token");
    }
    // Provider bodies/errors and long-lived credentials must never be logged
    // or passed through. Only the temporary credential belongs in this reply.
    return Response.json({
      token,
      model: geminiModel,
      expires_at: expiresAt,
      new_session_expires_at: newSessionExpiresAt,
      websocket_url: geminiWebSocketUrl,
    }, { headers });
  } catch {
    return Response.json({ error: signal.aborted ? "provider_timeout" : "provider_unavailable" }, {
      status: signal.aborted ? 504 : 502, headers,
    });
  }
}

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
    const headers = { "Cache-Control": "no-store", "X-Companion-Revision": "gemini-session-v1" };

    if (pathname === "/device/check" || pathname === "/device/session") {
      if (request.method !== "POST") {
        return Response.json({ error: "method_not_allowed" }, {
          status: 405,
          headers: { ...headers, Allow: "POST" },
        });
      }
      const rejection = authenticateDevice(request, env, headers);
      if (rejection) return rejection;
      // Session policy is server-owned; client bodies and query options are
      // never parsed or forwarded to Google.
      const throttled = await enforceDeviceRateLimit(env, headers);
      if (throttled) return throttled;
      if (pathname === "/device/session") return createGeminiSession(env, headers);
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
