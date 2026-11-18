// The real API key stays in Cloudflare. This script holds only the device
// credential and the short-lived Gemini token, and logs neither one.
import { readFile, mkdir, writeFile } from "node:fs/promises";
import { parseArgs } from "node:util";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const hostedBase = "https://esp32-companion-control.stiener.workers.dev";
const model = "models/gemini-3.8-live";
const websocketUrl = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContentConstrained";
const root = new URL("../", import.meta.url);
const checks = [];
class CheckFailure extends Error {}
const voicePrompt = "In one short sentence, say hello and tell me you are ready to chat.";

async function loadAudioQuestion() {
  let pcm, metadata;
  try {
    pcm = await readFile(new URL("logs/gemini-input-question.pcm", root));
    metadata = JSON.parse(await readFile(new URL("logs/gemini-input-question.json", root), "utf8"));
  } catch { throw new CheckFailure("Prepare the fixed synthetic question with make_audio_fixture.py first."); }
  if (!pcm.length || pcm.length % 2 || pcm.length > 16000 * 2 * 6 ||
      metadata.source !== "Windows System.Speech synthetic fixture" || metadata.question !== "What is two plus two?" ||
      metadata.sample_rate !== 16000 || metadata.channels !== 1 || metadata.sample_bits !== 16 ||
      metadata.bytes !== pcm.length || metadata.microphone_opened !== false ||
      metadata.sha256 !== createHash("sha256").update(pcm).digest("hex")) {
    throw new CheckFailure("The fixed audio fixture failed format, size or identity validation.");
  }
  return { pcm, metadata };
}

function wavFile(pcm) {
  const header = Buffer.alloc(44);
  header.write("RIFF", 0);
  header.writeUInt32LE(36 + pcm.length, 4);
  header.write("WAVEfmt ", 8);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22);
  header.writeUInt32LE(24000, 24);
  header.writeUInt32LE(48000, 28);
  header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36);
  header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}

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

export async function connect(token, { speak = false, fixture = null } = {}, runtime = {}) {
  // Gemini's constrained WebSocket protocol requires the ephemeral credential
  // as access_token. Construct the URL only in memory; never print it/errors.
  const url = new URL(websocketUrl);
  url.searchParams.set("access_token", token);
  const expectReply = speak || Boolean(fixture);
  const Socket = runtime.WebSocket || WebSocket;
  const pause = runtime.pause || (ms => new Promise(resolve => setTimeout(resolve, ms)));
  return await new Promise((resolve, reject) => {
    const socket = new Socket(url);
    let settled = false;
    let setupReceived = false;
    let receivedBytes = 0;
    const audioChunks = [];
    const transcriptParts = [];
    let transcriptLength = 0;
    let inputBytesSent = 0;
    let inputChunksSent = 0;
    let inputStreamEnded = !fixture;
    let turnCompleteSeen = false;
    let messageQueue = Promise.resolve();
    function finish(error) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      try { socket.close(1000); } catch { /* Do not surface native errors. */ }
      if (error) reject(new CheckFailure(error));
      else resolve({ pcm: Buffer.concat(audioChunks), transcript: transcriptParts.join(""), inputBytesSent, inputChunksSent, inputStreamEnded });
    }
    const timer = setTimeout(() => finish(expectReply ? "Gemini voice turn did not complete within 30 seconds." : "Gemini setup did not complete within 15 seconds."), expectReply ? 30000 : 15000);
    async function sendAudioQuestion() {
      // 40 ms frames, paced at 16 kHz. Send raw PCM, never its WAV header or
      // a text copy of the question. Stop immediately on failure/closure.
      for (let offset = 0; offset < fixture.pcm.length; offset += 1280) {
        if (settled) return;
        if (socket.readyState !== 1 || socket.bufferedAmount > 65536) throw new CheckFailure("Audio sender unavailable.");
        const chunk = fixture.pcm.subarray(offset, offset + 1280);
        socket.send(JSON.stringify({ realtimeInput: { audio: { data: chunk.toString("base64"), mimeType: "audio/pcm;rate=16000" } } }));
        inputBytesSent += chunk.length;
        inputChunksSent++;
        await pause(chunk.length / 32);
      }
      if (settled) return;
      socket.send(JSON.stringify({ realtimeInput: { audioStreamEnd: true } }));
      inputStreamEnded = true;
      if (turnCompleteSeen && receivedBytes) finish();
    }
    socket.addEventListener("open", () => {
      // The token locks the full policy. No microphone audio is sent.
      try { socket.send(JSON.stringify({ setup: { model } })); }
      catch { finish("Gemini closed before the setup message could be sent."); }
    });
    async function handleMessage(event) {
      if (settled) return;
      try {
        const raw = typeof event.data === "string" ? event.data : await event.data.text();
        if (raw.length > (expectReply ? 262144 : 16384)) return finish("Unexpected oversized Gemini reply.");
        const message = JSON.parse(raw);
        if (Object.hasOwn(message, "error")) return finish("Google rejected the constrained Gemini request; check project access and quota.");
        if (Object.hasOwn(message, "toolCall")) return finish("Unexpected tool request in a tools-disabled test.");
        if (Object.hasOwn(message, "setupComplete")) {
          if (!expectReply) return finish();
          if (setupReceived) return finish("Unexpected repeated Gemini setup acknowledgment.");
          setupReceived = true;
          if (fixture) {
            void sendAudioQuestion().catch(() => finish("Could not finish sending the fixed audio question."));
          } else {
            socket.send(JSON.stringify({ clientContent: {
              turns: [{ role: "user", parts: [{ text: voicePrompt }] }], turnComplete: true,
            } }));
          }
        }
        if (expectReply && message.serverContent) {
          if (!setupReceived) return finish("Gemini sent voice content before setup completed.");
          const content = message.serverContent;
          if (content.interrupted) return finish("Gemini voice turn was unexpectedly interrupted.");
          if (fixture && content.outputTranscription?.text !== undefined) {
            const text = content.outputTranscription.text;
            if (typeof text !== "string" || transcriptLength + text.length > 2048) return finish("Unexpected oversized or invalid reply transcript.");
            transcriptLength += text.length;
            transcriptParts.push(text);
          }
          for (const part of content.modelTurn?.parts || []) {
            if (!part.inlineData) continue;
            const { mimeType, data } = part.inlineData;
            if (mimeType !== "audio/pcm;rate=24000" || typeof data !== "string" ||
                !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(data)) {
              return finish("Gemini returned an unexpected audio format.");
            }
            const chunk = Buffer.from(data, "base64");
            receivedBytes += chunk.length;
            if (chunk.length % 2 || receivedBytes > 24000 * 2 * 20) return finish("Gemini audio exceeded the bounded PCM response.");
            audioChunks.push(chunk);
          }
          if (content.turnComplete) {
            if (!receivedBytes) return finish("Gemini completed the turn without audio.");
            turnCompleteSeen = true;
            if (inputStreamEnded) finish();
          }
        }
      } catch {
        finish("Gemini returned an invalid response.");
      }
    }
    socket.addEventListener("message", event => {
      // Binary JSON messages arrive as Blobs; serialize their async decoding
      // so a turn-complete message cannot overtake an earlier audio chunk.
      messageQueue = messageQueue.then(() => handleMessage(event)).catch(() => finish("Could not process the Gemini reply."));
    });
    socket.addEventListener("error", () => finish("Gemini WebSocket connection failed; check project access and network."));
    socket.addEventListener("close", event => {
      if (!settled) finish(`Gemini closed before the check completed (code ${Number(event.code)}).`);
    });
  });
}

async function main() {
  let values;
  try {
    ({ values } = parseArgs({ options: {
      "base-url": { type: "string", default: hostedBase },
      name: { type: "string" },
      connect: { type: "boolean", default: false },
      speak: { type: "boolean", default: false },
      "audio-question": { type: "boolean", default: false },
    } }));
  } catch { throw new CheckFailure("Use only --base-url ORIGIN, --name BASENAME, --connect, --speak or --audio-question."); }
  if (values.speak && values["audio-question"]) throw new CheckFailure("Choose either the text greeting or the audio question.");
  values.name ??= values["audio-question"] ? "worker-gemini-audio-check" : values.speak ? "worker-gemini-voice-check" : "worker-gemini-session-check";
  if (!/^[A-Za-z0-9_-]+$/.test(values.name)) throw new CheckFailure("Use a simple letters/digits/hyphens/underscores log name.");
  const base = baseOrigin(values["base-url"]);
  const fixture = values["audio-question"] ? await loadAudioQuestion() : null;
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
  let answer;
  if (values.connect || values.speak || fixture) {
    answer = await connect(payload.token, { speak: values.speak, fixture });
    pass("Gemini accepted the constrained WebSocket setup", { microphone_audio_sent: false, prompt_sent: values.speak });
  }
  payload.token = undefined;
  const folder = new URL("logs/", root);
  await mkdir(folder, { recursive: true });
  if (values.speak || fixture) {
    await writeFile(new URL(`${values.name}.wav`, folder), wavFile(answer.pcm));
    pass("one bounded Gemini spoken reply saved", { bytes: answer.pcm.length, sample_rate: 24000, channels: 1, sample_bits: 16, seconds: answer.pcm.length / 48000 });
  }
  if (fixture) {
    await writeFile(new URL(`${values.name}.transcript.txt`, folder), answer.transcript + "\n");
    if (answer.inputBytesSent !== fixture.pcm.length || !answer.inputStreamEnded || !/\b(?:four|4)\b/i.test(answer.transcript)) {
      throw new CheckFailure("Audio exchange finished, but the complete input/expected answer was not verified.");
    }
    pass("fixed audio question received the expected answer", {
      input_bytes: answer.inputBytesSent, input_chunks: answer.inputChunksSent, input_sample_rate: 16000,
      input_sha256: fixture.metadata.sha256, audio_stream_end_sent: true, microphone_audio_sent: false, text_question_sent: false,
    });
  }
  await writeFile(new URL(`${values.name}.log`, folder), JSON.stringify({
    base_url: base, checked_at: new Date().toISOString(), checks,
  }, null, 2) + "\n");
  console.log("Session checks passed. No credential or temporary token was printed or logged.");
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main().catch(error => {
  // Only our controlled messages reach the terminal; never print native stacks.
  const safe = error instanceof CheckFailure ? error.message : "Unexpected checker failure.";
  console.error(`Session check failed: ${safe}`);
  process.exitCode = 1;
});
