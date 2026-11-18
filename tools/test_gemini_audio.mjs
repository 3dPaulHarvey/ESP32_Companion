import assert from "node:assert/strict";
import { test } from "node:test";
import { connect } from "./check_gemini_session.mjs";

const publicToken = "auth_tokens/public-fixture-not-a-credential";
const replyPcm = Buffer.from([1, 0, 255, 255]);

function fakeRuntime(mode = "normal") {
  const instances = [];
  const pauses = [];
  class FakeSocket extends EventTarget {
    readyState = 0;
    bufferedAmount = 0;
    sent = [];
    closes = 0;
    constructor() {
      super();
      instances.push(this);
      queueMicrotask(() => {
        this.readyState = 1;
        this.dispatchEvent(new Event("open"));
      });
    }
    message(value) {
      this.dispatchEvent(new MessageEvent("message", { data: JSON.stringify(value) }));
    }
    send(raw) {
      const value = JSON.parse(raw);
      this.sent.push(value);
      if (value.setup) {
        if (mode === "backpressure") this.bufferedAmount = 65537;
        queueMicrotask(() => this.message({ setupComplete: {} }));
      } else if (mode === "close-during-input" && value.realtimeInput?.audio) {
        this.readyState = 3;
        const event = new Event("close");
        Object.defineProperty(event, "code", { value: 1006 });
        this.dispatchEvent(event);
      } else if (value.realtimeInput?.audioStreamEnd || value.clientContent) {
        queueMicrotask(() => this.message({ serverContent: {
          outputTranscription: { text: "Two plus two is four." },
          modelTurn: { parts: mode === "empty-reply" ? [] : [{ inlineData: {
            mimeType: "audio/pcm;rate=24000", data: replyPcm.toString("base64"),
          } }] },
          turnComplete: true,
        } }));
      }
    }
    close() { this.closes++; this.readyState = 3; }
  }
  return { instances, pauses, WebSocket: FakeSocket, pause: async ms => { pauses.push(ms); } };
}

test("audio-only question sends exact raw PCM frames, pacing and EOF without text", async () => {
  const runtime = fakeRuntime();
  const pcm = Buffer.from(Array.from({ length: 2720 }, (_, index) => index % 256));
  const result = await connect(publicToken, { fixture: { pcm } }, runtime);
  const socket = runtime.instances[0];
  const input = socket.sent.slice(1);
  assert.equal(input.length, 4);
  const chunks = input.slice(0, 3).map(item => {
    assert.deepEqual(Object.keys(item), ["realtimeInput"]);
    assert.deepEqual(Object.keys(item.realtimeInput), ["audio"]);
    assert.equal(item.realtimeInput.audio.mimeType, "audio/pcm;rate=16000");
    return Buffer.from(item.realtimeInput.audio.data, "base64");
  });
  assert.deepEqual(chunks.map(chunk => chunk.length), [1280, 1280, 160]);
  assert.deepEqual(Buffer.concat(chunks), pcm);
  assert.deepEqual(input[3], { realtimeInput: { audioStreamEnd: true } });
  assert.deepEqual(runtime.pauses, [40, 40, 5]);
  assert.equal(result.inputBytesSent, pcm.length);
  assert.equal(result.inputChunksSent, 3);
  assert.equal(result.inputStreamEnded, true);
  assert.equal(result.transcript, "Two plus two is four.");
  assert.deepEqual(result.pcm, replyPcm);
  assert.equal(socket.closes, 1);
});

test("socket backpressure stops audio before any frame is sent", async () => {
  const runtime = fakeRuntime("backpressure");
  await assert.rejects(connect(publicToken, { fixture: { pcm: Buffer.alloc(2720) } }, runtime), /Could not finish sending/);
  assert.equal(runtime.instances[0].sent.length, 1);
  assert.equal(runtime.instances[0].closes, 1);
});

test("a connection closing mid-input stops later frames and EOF", async () => {
  const runtime = fakeRuntime("close-during-input");
  await assert.rejects(connect(publicToken, { fixture: { pcm: Buffer.alloc(2720) } }, runtime), /closed before the check completed/);
  assert.equal(runtime.instances[0].sent.length, 2);
  assert.equal(runtime.instances[0].sent.at(-1).realtimeInput.audioStreamEnd, undefined);
  assert.equal(runtime.instances[0].closes, 1);
});

test("a completed turn without output audio fails rather than saving a silent success", async () => {
  const runtime = fakeRuntime("empty-reply");
  await assert.rejects(connect(publicToken, { fixture: { pcm: Buffer.alloc(1280) } }, runtime), /without audio/);
  assert.equal(runtime.instances[0].closes, 1);
});

test("setup-only mode still sends just setup and closes", async () => {
  const runtime = fakeRuntime();
  const result = await connect(publicToken, {}, runtime);
  assert.equal(runtime.instances[0].sent.length, 1);
  assert.equal(result.pcm.length, 0);
  assert.equal(result.inputBytesSent, 0);
  assert.equal(runtime.instances[0].closes, 1);
});

test("text-greeting mode still sends one user turn and receives audio", async () => {
  const runtime = fakeRuntime();
  const result = await connect(publicToken, { speak: true }, runtime);
  const socket = runtime.instances[0];
  assert.equal(socket.sent.length, 2);
  assert.equal(socket.sent[1].clientContent.turnComplete, true);
  assert.equal(socket.sent[1].clientContent.turns[0].role, "user");
  assert.equal(socket.sent[1].clientContent.turns[0].parts[0].text, "In one short sentence, say hello and tell me you are ready to chat.");
  assert.equal(result.inputBytesSent, 0);
  assert.deepEqual(result.pcm, replyPcm);
});
