// Public synthetic two-turn context check. No microphone, saved reply or transcript.
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
const root = new URL('../', import.meta.url);
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
class CheckFailure extends Error {}
const check = (ok, message) => { if (!ok) throw new CheckFailure(message); };
let socket;
const report = { microphone_opened: false, transcript_saved: false, response_audio_saved: false, websocket_connections: 0, turns: [] };
try {
  const inputs = await Promise.all(['robot-question-public-fixture.pcm', 'robot-followup-public-fixture.pcm'].map(name => readFile(new URL(`logs/${name}`, root))));
  const info = JSON.parse(await readFile(new URL('logs/robot-followup-public-fixture.json', root), 'utf8'));
  const firstSource = await readFile(new URL('logs/gemini-input-question.pcm', root));
  const firstInfo = JSON.parse(await readFile(new URL('logs/gemini-input-question.json', root), 'utf8'));
  check(firstInfo.source === 'Windows System.Speech synthetic fixture' && firstInfo.question === 'What is two plus two?' &&
    firstInfo.microphone_opened === false && firstSource.length <= 128000 && firstInfo.sha256 === createHash('sha256').update(firstSource).digest('hex') &&
    inputs[0].equals(Buffer.concat([firstSource, Buffer.alloc(128000 - firstSource.length)])), 'First public fixture failed identity verification.');
  check(inputs.every(pcm => pcm.length === 128000) && info.microphone_opened === false &&
    info.question === 'And what is that number times three?' && info.sha256 === createHash('sha256').update(inputs[1]).digest('hex'), 'Prepare verified public fixtures first.');
  let text = await readFile(new URL('cloudflare-worker/.dev.vars', root), 'utf8');
  const matches = [...text.matchAll(/^DEVICE_AUTH_TOKEN="([0-9a-f]{64})"[ \t]*\r?$/gm)]; text = undefined;
  check(matches.length === 1, 'Expected one local device credential.');
  const response = await fetch('https://esp32-companion-control.stiener.workers.dev/device/session', {
    method: 'POST', headers: { Authorization: `Bearer ${matches[0][1]}` }, redirect: 'manual', signal: AbortSignal.timeout(15000),
  });
  check(response.status === 200 && response.headers.get('x-companion-revision') === 'gemini-session-v1' && response.headers.get('cache-control') === 'no-store', 'Session request failed.');
  const payload = await response.json();
  const endpoint = 'wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContentConstrained';
  check(payload.websocket_url === endpoint && payload.model === 'models/gemini-3.8-live' && /^[\x21-\x7e]{16,8192}$/.test(payload.token), 'Invalid session response.');
  const url = new URL(endpoint); url.searchParams.set('access_token', payload.token); payload.token = undefined;
  let failed = null, setup = false, current = null;
  socket = new WebSocket(url);
  ++report.websocket_connections;
  socket.binaryType = 'arraybuffer';
  socket.addEventListener('error', () => { failed = 'WebSocket transport failed; details suppressed.'; });
  socket.addEventListener('message', event => {
    try {
      const data = typeof event.data === 'string' ? event.data : event.data instanceof ArrayBuffer
        ? new TextDecoder('utf-8', { fatal: true }).decode(event.data) : null;
      check(data !== null && data.length <= 65536, 'Invalid or oversized server message.');
      const message = JSON.parse(data);
      check(!message.error && !message.toolCall, 'Unexpected rejection or tool request.');
      if (message.setupComplete) { setup = true; return; }
      const content = message.serverContent;
      if (!content) return;
      check(current, 'Received content outside a test turn.');
      if (typeof content.outputTranscription?.text === 'string') {
        check(current.text.length + content.outputTranscription.text.length <= 4096, 'Reply text exceeded bound.');
        current.text += content.outputTranscription.text;
      }
      for (const part of content.modelTurn?.parts || []) {
        if (!part.inlineData) continue;
        check(part.inlineData.mimeType === 'audio/pcm;rate=24000', 'Unexpected reply format.');
        const bytes = Buffer.from(part.inlineData.data, 'base64');
        current.audio_bytes += bytes.length;
        check(bytes.length % 2 === 0 && current.audio_bytes <= 480000, 'Invalid or oversized reply audio.');
      }
      if (content.interrupted) throw new CheckFailure('The synthetic turn was interrupted.');
      if (content.turnComplete) current.complete = true;
    } catch (error) { failed = error instanceof CheckFailure ? error.message : 'Unexpected server message; contents suppressed.'; }
  });
  const until = async (condition, seconds) => {
    const deadline = Date.now() + seconds * 1000;
    while (!condition()) {
      check(!failed, failed || 'Transport failed.');
      check(Date.now() < deadline && socket.readyState !== WebSocket.CLOSED, 'Timed out or closed before expected completion.');
      await pause(10);
    }
  };
  await until(() => socket.readyState === WebSocket.OPEN, 15);
  socket.send(JSON.stringify({ setup: { model: 'models/gemini-3.8-live' } }));
  await until(() => setup, 20); report.setup_ack = true;
  for (let index = 0; index < 2; ++index) {
    current = { text: '', audio_bytes: 0, complete: false };
    let chunks = 0;
    for (let offset = 0; offset < 128000; offset += 1280) {
      check(!failed, failed || 'Transport failed.');
      socket.send(JSON.stringify({ realtimeInput: { audio: { mimeType: 'audio/pcm;rate=16000', data: inputs[index].subarray(offset, offset + 1280).toString('base64') } } }));
      ++chunks; await pause(40);
    }
    socket.send(JSON.stringify({ realtimeInput: { audioStreamEnd: true } }));
    await until(() => current.complete, 35);
    await pause(index === 0 ? 4500 : 500);
    const expected = index === 0 ? /\b(?:four|4)\b/i : /\b(?:twelve|12)\b/i;
    const correct = expected.test(current.text);
    report.turns.push({ turn: index + 1, input_bytes: 128000, input_chunks: chunks, input_stream_ended: true, reply_audio_bytes: current.audio_bytes, turn_complete: current.complete, expected_answer_present: correct });
    current.text = '';
    check(correct && current.audio_bytes > 0 && !failed, 'Synthetic context answer or complete audio check failed.');
  }
  const closed = new Promise(resolve => socket.addEventListener('close', event => resolve(event.code), { once: true }));
  socket.close(1000); report.close_code = await Promise.race([closed, pause(4000).then(() => 0)]);
  check(report.close_code === 1000, 'Normal close was not acknowledged.');
  report.passed = true;
} catch (error) {
  report.passed = false;
  report.failure = error instanceof CheckFailure ? error.message : 'Context check stopped; private transport details suppressed.';
  try { socket?.close(); } catch {}
  process.exitCode = 1;
} finally {
  report.recorded_at = new Date().toISOString();
  await writeFile(new URL('logs/gemini-followup-context.json', root), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report));
}
