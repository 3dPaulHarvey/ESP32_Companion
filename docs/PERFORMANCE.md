# Response performance

Local audit, 2026-11-19. The target remains the existing XIAO ESP32-S3, its microphone, speaker and OLED. The largest opportunities are in the order of network/audio work and intentional waits; the current evidence does not establish a need for more local compute or a different provider.

The audit measurements below are historical. The subsequent live implementation and device checks are summarized first. Real voice latency still needs a paired hardware comparison; synthetic timing must not be generalized to natural speech.

## Live streaming implementation, 2026-11-19

The subsequent 0.3.0 implementation adds all six local changes: connection setup during question capture, incremental PCM playback with a 120 ms prebuffer, live microphone upload, local speech endpointing, bounded connection retention between wakes, and disabled Wi-Fi modem power saving. It also removes the question cue and its 800 ms wait. The deployed Worker and Gemini free-tier project/model remain unchanged.

Speech endpointing uses 20 ms VAD frames, the retained sustained-speech acceptance thresholds, 320 ms pre-roll, 600 ms trailing silence, a four-second no-speech wait and a ten-second total capture limit. The normal path now retains one transport across up to eight answers, a 30-second idle window and a two-minute absolute lifetime. Quiet input remains local. Failures close the stream without replaying an uncertain question. Provider shutdown notices stop new turn admission. The old fixed four-second diagnostic/fixture paths remain available separately.

The first complete new hardware synthetic run (`%LOCALAPPDATA%/ESP32_Companion/qa/live-stream-fixture-hardware-4.json`) passed four answers, two locally discarded quiet inputs and a warm re-entry on one credential request/one WebSocket. All 512,000 input bytes were accepted exactly once; output accounting matched all 359,523 played samples with zero speaker underruns and zero clipping. Close code was 1000. The run also recovered injected short/temporary TLS writes with no question retry. These public PCM fixtures deliberately bypass room microphone/VAD and cannot establish real voice endpoint quality.

That run measured 5.644–5.816 s uploads for four-second synthetic inputs, despite individual writes taking at most 19 ms. Further network experiments did not establish a consistent improvement. Nonblocking receive polling encountered a write timeout; larger TCP windows with TCP_NODELAY passed four turns but uploaded more slowly; combined WebSocket header/payload writes encountered a partial-frame timeout. Those variants were not retained. Their separate numeric reports remain in LocalAppData for investigation. The selected release uses the measured conservative TCP windows, the established SDK WebSocket sender and a bounded receive pump, with modem power saving disabled.

The final selected synthetic check (`%LOCALAPPDATA%/ESP32_Companion/qa/live-selected-fixture-hardware-final.json`) passed two replies, one quiet discard, exact input/output counts, zero speaker underruns and normal close 1000. Its four-second uploads took 5.770 and 5.717 s. In its second turn, playback began 0.768 s before generation completion and 2.399 s before server turn completion. This demonstrates actual incremental reply playback. The warm fixture's recording-end-to-first-queued-speaker-PCM measurement was 1.760 s; the synthetic fixture bypasses local VAD and has a prerecorded trailing silence, so this is not a natural-speech response claim.

Hardware testing also found that the configured newlib nano `printf` emits `ld` for `%lld`. Monotonic counters now use an explicit bounded decimal formatter, with compile-time checks at zero, beyond 32-bit uptime and the full unsigned 64-bit limit. This preserves valid timing data without changing the printf library or hardware.

One atomic report replacement returned `PermissionError` in the Dropbox-backed `logs/` folder. Active reports now use LocalAppData and replacement has a bounded retry. Dropbox involvement is suspected, not confirmed; no source-file sync failure was observed. Early observer failures and incomplete evidence are retained separately, rather than counted as clean passes.

The selected normal v0.3.0 image is now uploaded to COM26: 1,954,224 bytes, SHA256 `4b4910416677b6aa3aed9909c202992150db24220ad2778a70dc453853989a2d`. It passes 206 compiled policy assertions across twelve suites, both local runtime suites, all 97 host/tool tests, the full normal/fixture firmware builds and the existing preflash checks. Normal-image recovery artifacts are archived separately under `recovery/2026-11-19-performance/live-stream-normal/`; original recovery files and NVS remain intact. Clean selected/warm synthetic evidence is also copied once to `logs/live-upgrade-selected-fixture.json` and `logs/live-upgrade-warm-fixture.json`. Hardware startup/control checks and human voice confirmation are recorded separately.

All eight normal-image device checks subsequently passed (`logs/live-upgrade-normal-device.json`), including Wi-Fi, diagnostic control recovery and local calibration without a provider call. The user then confirmed correct four/twelve voice replies and reported continued fast, reliable conversations. The two complete logged voice captures took 2.700 and 2.371 s including warmup; they uploaded 59,520 and 64,000 bytes, with exact playback and zero clipping/underruns. The measured last-VAD-speech-frame to first queued speaker PCM was 5.545 s cold and 2.170 s on the existing connection. These are two samples, not a statistical benchmark or a paired old/new measurement.

The same observer recorded a third accepted voice segment followed by `audio_send_failed` after 16,640 accepted bytes: the next write hit its one-second budget with no TLS error recorded. The user clarified that they deliberately continued talking, so this was not evidence of false-positive quiet detection. The observer later reported an unclassified serial/report access interruption. Preserve both events; neither the human confirmation nor two successful turns makes the entire recorded job clean. The board returned to wake recovery without automatically replaying the question. The user reports subsequent ongoing operation works well; later exchanges were not recorded by that observer. No additional settling pause or transport-budget change was installed after that clarification.

The reviewed voice evidence is in `logs/live-upgrade-human-review.json` and `logs/live-upgrade-room-test.json`. Longer continuous voice runs, clean real quiet closure, prolonged idle and access-point outage checks remain reliability follow-ups. The installed timing and verified network path are retained as the next measurement baseline.

## Current path

The normal standalone workflow already keeps Wi-Fi connected at READY. Both accepted questions in a conversation use one Gemini WebSocket and its context. A fresh wake after the conversation closes creates a new session; keeping Wi-Fi connected does not retain that Gemini connection.

Before this performance patch, the first-turn path was sequential:

1. Recognize the local wake word, warm up the microphone for 200 ms, record a fixed four-second segment, and finish the 800 ms cue state.
2. Hand the completed capture to the session task.
3. Obtain a fresh SNTP synchronization, fetch a one-use temporary token from the authenticated Worker over HTTPS, and release the Worker HTTP/TLS client.
4. Open the direct Gemini TLS/WebSocket connection and wait for setup acknowledgment.
5. Upload the already recorded segment as 100 frames of 40 ms, then send `audioStreamEnd`.
6. Accumulate the entire bounded reply until `turn_complete`, then play it through the speaker.
7. Capture and classify the next fixed four-second segment locally. Upload accepted follow-up speech on the same connection; discard quiet locally. Close after quiet or the two-answer limit.

Relevant code: [capture handoff](../Source/esp32-companion/main/hardware_diagnostics.cc), [time synchronization/session acquisition](../Source/esp32-companion/main/companion/session_probe.cc), [conversation connection/upload/playback](../Source/esp32-companion/main/companion/gemini_conversation.cc). The single-question diagnostic path remains separate from the normal bounded conversation transport.

Before the first performance patch, the conversation upload scheduled each 40 ms wait from the time **after** encoding and sending the frame. That adds send/processing time repeatedly to a stream already recorded in full. Its receive pump can also contribute scheduling overhead. Reply playback waited for server turn completion even though output generation can finish earlier.

## What the saved evidence measures

| Stage | Historical observation | Sample and limitation |
|---|---|---|
| Four-second microphone capture | 4.112–4.125 s including capture overhead | Fifteen measured captures in the reviewed real standalone session, including its failed attempt; not the current image |
| Successful Worker session acquisition | 2.750, 3.364 and 3.953 s | Three successful session-only checks from two older hardware runs; includes device-side work such as time synchronization, rather than isolating Worker CPU or provider token issuance |
| Direct Gemini setup/close | 1.915–2.240 s | Six final successful setup/close checks across a full check and short repeat; excludes preceding Worker request and includes close work |
| Upload of four seconds of retained PCM | Minimum 5.052 s, median 5.2555 s, maximum 6.695 s | Twenty uploads in ten controlled synthetic conversations with partial/temporary-write faults injected once per conversation; no live microphone input |
| Individual sends in those uploads | Maximum 89 ms; zero sends at least 250 ms | Same twenty synthetic uploads; supports investigating local pacing rather than assuming bandwidth saturation |
| One real manual question, entire session job | 17.106 s, including 3.965 s playback/settling for a 3.800 s reply | One verified real question; initial capture excluded; not time to first audible reply |
| Standalone one-answer quiet trial, entire job | 23.993 s, including a 4.240 s spoken answer and final quiet capture | One verified real trial; initial capture excluded |
| Standalone two-answer quiet-ending jobs | 36.105 and 40.186 s | Two verified real gate trials; includes both spoken answers, follow-up capture and final quiet capture; initial capture excluded |

Evidence and retained reviews:

- [Session checkpoint](ESP32_SESSIONS.md), [direct setup checkpoint](ESP32_GEMINI_SETUP.md), and ignored `logs/esp32-session-hardware-verified.log`, `logs/esp32-gemini-setup-hardware-verified.log`, `logs/esp32-gemini-setup-short-repeat.log`.
- [Verified live question](ESP32_QUESTIONS.md) and ignored `logs/robot-question-tests/2026-11-18_14-00-56_4f328c47/session.json`.
- [Reviewed standalone results](ESP32_STANDALONE.md) and ignored `logs/robot-standalone-tests/2026-11-18_17-46-59_cc36a560/session.json`. The source session contains six attempts, including one upload failure; five completed their protocol exchange and received human confirmation. The table uses the three clean gate trials rather than treating all attempts as an uninterrupted reliability run.
- [Upload-recovery procedure and limits](UPLOAD_RECOVERY.md), ignored `logs/esp32-upload-recovery-ten-cycles.json`, and its review `logs/esp32-upload-investigation.json`. Upload median was calculated from all twenty saved `upload_ms` fields.

The November 19 observer session `2026-11-19_08-53-07_3332717b` has no recorded attempts. It supplies no new response-time measurement.

## Measure the delay the user experiences

The primary response metric should be **capture end to first audible reply**, with **wake recognition to first audible reply** recorded separately. An eventual variable-length capture should also record detected speech end, because a fixed window can keep recording after the user finishes speaking.

Keep these distinct from complete job duration. The existing conversation task timer starts after first-capture handoff and finishes after playback, later captures, quiet classification and close. A 36-second two-answer job therefore does not mean the first answer took 36 seconds to begin.

Record monotonic, numeric milestones for:

- Time-sync cache hit or fresh-sync duration, Worker response duration, TLS/WebSocket upgrade, and setup acknowledgment.
- Input upload start/end, accepted bytes/frames, maximum send time and temporary-write recovery.
- First decoded reply audio, reply completion, first speaker PCM queued, playback completion and buffer underruns if streaming playback is introduced.
- Initial capture completion, turn number, connection reuse and full job completion.

The existing observer already saves numeric capture, upload, playback and whole-job diagnostics without storing room audio or transcripts. Extend that format for new timing fields. A timestamp for queuing the first speaker PCM is a useful device proxy; physical audio measurement is needed to quantify any additional output/DMA delay precisely. Existing logs do not isolate first incoming audio or provider inference time.

Compare a consistent short question over several fresh wakes and separately compare second turns on the established connection. Record medians, slow cases, failures, Wi-Fi RSSI, output duration and whether time synchronization or a new connection was required. Use the existing synthetic fixture for transport/fault regression, and keep its results separate from real voice checks. Do not rerun a captured question automatically after uncertain network failure.

## Prioritized changes

### 1. Remove repeated local overhead and add timing

The first bounded patch reuses a successful time synchronization for less than ten minutes, measured with monotonic uptime, and rejects more than two seconds of wall-clock drift. The first session after boot and cache expiry still synchronize before temporary-token validation. A plausible wall-clock date alone cannot bypass synchronization: earlier testing found a device clock 216.6 seconds ahead and rejected fresh tokens. Certificate date/hostname checks and temporary-token expiry checks remain in force.

Both conversation and single-question diagnostic uploads now schedule each frame's 40 ms interval from its send-start time, so sending is not followed by a second full frame interval. Slow writes do not cause catch-up bursts. The conversation transport always services the receive side at least once per frame, including after slow writes. Exact PCM bytes, frame order, EOF, cancellation and bounded partial-write handling are retained. This pacing correction does not yet overlap capture and upload.

The conversation starts whole-buffer playback when `generationComplete` arrives, falling back to `turnComplete` if that earlier signal is absent. The PCM buffer becomes immutable at generation completion; the receiver continues handling control and metadata while the speaker runs. Both actual playback and server turn completion must finish before follow-up capture. This avoids serializing physical playback after the provider's assumed playback wait; it is not incremental playback yet. Google's [server-content reference](https://aistudio.google.com/api-reference/live#BidiGenerateContentServerContent) documents these separate milestones.

Numeric `FETCH`, `CONNECTION_TIMING`, `PLAYBACK_BEGIN`, and per-turn `TIMING` logs now separate session acquisition, upgrade/setup, upload, first received PCM, generation completion, turn completion and playback request. Per-turn offsets start at upload start; `PLAYBACK_BEGIN` also contains monotonic microseconds. A zero timing field means its milestone was not observed. Playback request is a device proxy, not an acoustic measurement. The existing observer parses and saves these optional fields, including on partial trials, and still accepts older traces. Existing capture logs allow device-log alignment across stages. Separate SNTP timing remains in local logs.

The historical median upload exceeded the nominal four seconds by about 1.26 s. That gap is an opportunity to measure, not a promised saving: pump timeouts, scheduling and network behavior still matter. Fresh-sync savings are also unknown until synchronization is timed separately.

The installed Wi-Fi manager exposes `SetPowerSaveLevel(PERFORMANCE)`, but the audited application does not call it. Test that setting as a separate controlled experiment during conversation and, if appropriate, restore the idle policy afterward. Measure response-time distribution and power/thermal behavior; disabling power saving has no measured benefit in this project yet.

### 2. Open the connection while the user speaks

Begin authenticated session preparation at an accepted wake while the existing capture task continues. Hand the completed PCM to an already prepared connection. This could hide part of the session/setup work under the recording window without changing microphone format or requiring larger hardware.

It requires explicit ownership and cancellation when capture fails, Wi-Fi disappears, control changes or a token approaches its connection-start deadline. Do not extend a temporary token's validity or reuse its one-use credential for a second connection. Concurrent audio capture and the two connection phases need heap-watermark checks, since the current implementation releases Worker TLS before allocating Gemini TLS deliberately.

### 3. Play reply audio incrementally

The first patch advances whole-buffer playback to generation completion. The next change should feed validated incoming PCM into a bounded speaker queue after a small measured prebuffer, while the network owner continues receiving. Start speaking before the full answer is generated and received. Keep the ten-second/output-byte bounds, audio validation, exact accounting and cleanup behavior.

Use queue ownership or a ring buffer so playback cannot read memory being reset by the next turn. Measure underruns and queue depth. Do not block network reception for the entire playback or let slow playback create unbounded output storage. This change targets first audible response; total spoken duration naturally remains.

### 4. Stream microphone PCM and end turns on speech completion

Replace the record-then-replay upload with bounded live frames once the connection is ready. This removes a second real-time traversal of the captured audio. Use measured speech/silence endpointing with a maximum utterance duration so a short question can finish before the fixed four-second limit.

Preserve the existing local quiet-follow-up policy. Hold a bounded initial pre-roll until local speech is accepted, then send the retained leading audio and continue live; discard a quiet window locally. The current speech gate uses an entire completed segment, so moving it online is a behavior change requiring tests for quiet rooms, background noise, leading consonants and pauses. Preserve the calibrated capture gain and wake gain unless separate intelligibility measurements justify changing them.

### 5. Keep a short conversation connection ready

The robot already reuses one connection for two turns. A later experiment can retain a healthy connection for a bounded idle conversation window, continue pumping ping/control messages, and open a fresh one only when necessary. This could avoid repeated first-turn setup for nearby interactions.

Define idle/session limits, Wi-Fi recovery, reconnect backoff, server Retry-After handling and explicit context lifetime. Never reconnect and resend an utterance whose acceptance is uncertain. Provider session resumption is separate from TCP connection reuse and must be implemented against the provider's current documented semantics. Long-lived connectivity still needs a soak test and costs/quotas should be measured before making it the default.

### 6. Compare providers after the local baseline improves

Keep the working Gemini path as the comparison baseline and test another provider behind the same capture/playback timings. Compare first audible response, slow-case latency, answer quality, interruptions if enabled, memory use and measured cost per completed interaction. The user's existing OpenAI API credit makes a bounded A/B prototype practical, but credit balance alone does not establish a faster response path.

Provider switching cannot remove local record-then-upload or whole-reply buffering. Decide from measurements after addressing those stages.

## Connection and provider choices

The current Worker is a credential broker: audio travels directly between the ESP32 and Google. Increasing Worker CPU limits would not accelerate that audio path. Session acquisition includes device HTTPS, a Durable Object admission check and Google's token request; time those before changing placement or plans. Preserve this direct path unless a relay solves a measured problem.

Google recommends [small live chunks and minimal input buffering](https://aistudio.google.com/docs/live-api/best-practices#streaming-guidelines). No faster-than-realtime upload guarantee was found, so simply removing all pacing is not the first experiment. A live microphone pipeline fits the existing 16 kHz input and 24 kHz output.

A short persistent connection is practical, but the current Worker issues a one-use token with a one-minute start deadline and five-minute expiry. [Token expiry also constrains messages in established sessions](https://aistudio.google.com/api-reference/live#AuthToken). The firmware additionally enforces two turns and a two-minute network deadline. Extending those bounds requires explicit lifecycle handling. Google's [session-management guide](https://aistudio.google.com/docs/live-api/session-management) describes `GoAway`, resumption and compression; the current parser rejects `GoAway` and does not resume. The Worker locks the full setup, so enabling resumption or changing provider VAD also requires coordinated Worker policy changes. Preconnecting on wake can be tested within the present policy.

| Option | Why evaluate it | Cost or implementation tradeoff |
|---|---|---|
| Optimized Gemini Live over existing WebSocket | Smallest change; compatible microphone/speaker rates; working baseline | Fix overlap/buffering first; measure idle/listening billing before retaining sessions |
| OpenAI `gpt-realtime-2.1-mini` over WebSocket | Direct speech-to-speech; sensible first comparison using the existing API credit | Different protocol adapter; Realtime PCM input needs 24 kHz conversion; audio prices currently $10/M input and $20/M output tokens |
| OpenAI `gpt-realtime-2.1` | Compare quality and response timing against mini | Audio prices currently $32/M input and $64/M output tokens; price alone does not predict latency |
| WebRTC/Opus on the same ESP32-S3 | Media transport, compressed bandwidth, possible echo cancellation and interruption support | Larger firmware change; verify XIAO audio integration, internal heap and CPU headroom |
| Streaming STT, small text model, streaming TTS | Independent voice/model choice and explicit stage measurements | Adds separate services; naive sequential implementation can increase latency; pipeline it if tested |
| Small Raspberry Pi or a home-network gateway | Can simplify protocol handling, AEC and audio buffering while keeping the robot ESP32-sized | Adds a powered/network dependency; a Pi on the robot increases footprint and power; not justified by current evidence |

OpenAI references checked 2026-11-19: [mini model](https://developers.openai.com/api/docs/models/gpt-realtime-2.1-mini), [pricing](https://developers.openai.com/api/docs/pricing), [WebSocket/client credentials](https://developers.openai.com/api/docs/guides/voice-websockets?api=realtime), [Realtime sessions](https://developers.openai.com/api/docs/guides/realtime-conversations), and [cost accounting](https://developers.openai.com/api/docs/guides/realtime-costs). A Worker credential endpoint can keep the permanent OpenAI key off the device, as with Gemini today. The API credit is sufficient to justify a small measured comparison; account model access and actual usage still need verification. No provider call was made during this audit.

Espressif supplies an [ESP32-S3 OpenAI Realtime demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/openai_demo) with WebRTC, Opus and AEC. Its default board differs from this XIAO, so it demonstrates a possible small-compute route rather than a ready-made drop-in implementation. A Teensy would still need networking and would not remove cloud round trips. Current CPU frequency is already 240 MHz; compiler, TLS and Wi-Fi buffer tuning are secondary to the serial pipeline.

Keep prompts short and replies concise, benchmark model/turn-end settings after firmware improvements, and cache common local commands or acknowledgments where they provide real functionality. A quick local cue can communicate that a wake was heard; it should not be counted as an AI answer or replace the real latency metric.

## Experimental targets

For the first patch, demonstrate reduced upload overhead toward the four-second audio duration and removal of repeated fresh-sync work on cache hits, with unchanged exact transfer/playback and recovery checks. These are comparison criteria, not a claim of an achieved improvement.

For the streaming architecture, an established-session response beginning within roughly one to two seconds after detected speech end is a useful stretch experiment. Cold connection setup, network variability and model generation can exceed it. Track measured median and slow-case results before accepting any product target, and do not infer first-response latency from complete conversation totals.

## Local patch validation

Completed on 2026-11-19:

- All 86 host/tool tests pass, including the existing Tk workflow and two added timing-parser regressions.
- All nine native C++ policy suites (169 compiled assertions) and both local capture/state runtime suites pass. New assertions cover clock age/drift/failure boundaries, immutable PCM after generation completion, audio plus completion in one message, and the turn-completion fallback.
- Full ESP-IDF standalone build passes. The normal application is 1,937,728 bytes, SHA256 `2f0f481cf2935f0655cf07b2efaaf7fcbc94b0beb2c6a5d99abfc91ed8254ecd`, with 37% of its app partition free.
- Existing preflash checks pass; separate evidence is saved in ignored `logs/performance-local-preflash.json`. The image retains verified TLS, excludes synthetic inputs/fault injection and the private device credential, and preserves the NVS flash region and archived recovery evidence.

That first patch's validation preceded flashing. It was subsequently archived and uploaded during the live-upgrade work. Its observer window ended without a recorded physical wake, so no human voice confirmation was added. The live-upgrade synthetic checks above now exercise Gemini through the same deployed Worker. No Worker/API setting or billing configuration was changed.
