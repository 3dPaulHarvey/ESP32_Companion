# Cloudflare and Gemini: the next stage

The local hardware interaction has a successful physical follow-up. Hosted health, device authentication, shared request limits, temporary-token issuance, desktop audio exchange and direct Gemini speech through the robot's speaker are verified. [Current ESP32 checkpoint](ESP32_VOICE.md). No API key is needed for the public health check.

A **Cloudflare Worker** is a small program hosted by Cloudflare that responds to requests. We will use it as the robot's authentication/session endpoint; there is no virtual server for you to maintain. [Cloudflare Workers overview](https://developers.cloudflare.com/workers/)

**Gemini Live** will handle the spoken conversation. Our intended flow is:

```text
Robot -> Worker: authenticate and request a session
Worker -> Gemini: obtain a temporary access token
Worker -> Robot: return temporary token
Robot <-> Gemini Live: voice over a secure WebSocket
```

The permanent Gemini API key is stored as the Production **Worker Secret `GEMINI_API_KEY`**, accessible to the server program rather than embedded in the robot firmware. Keep it out of chat and Git. [Cloudflare secrets](https://developers.cloudflare.com/workers/configuration/secrets/)

Google documents the backend-token/client-WebSocket pattern for Gemini Live. Temporary tokens restrict a client's session access. This feature is currently preview. The implemented v1beta request locks one new session to Gemini 3.8 Live with a one-minute start deadline and five-minute token expiration. The hosted desktop setup acknowledgment passes. [Gemini Live ephemeral tokens](https://aistudio.google.com/docs/live-api/ephemeral-tokens)

We will verify the cloud stage in small steps:

1. **Done locally:** create a Worker with a simple health response and test its success/error routes.
2. **Done hosted:** the user deployed the health-only service and its HTTPS success/error responses passed verification.
3. **Done hosted:** device authentication rejects missing/wrong credentials and accepts the correct one.
4. **Done hosted:** the shared device limit rejects excess requests and recovers after its retry window.
5. **Done hosted:** store the Gemini key as a Worker Secret and verify temporary-token issuance.
6. **Desktop audio exchange done:** Gemini accepts the desktop setup, and the user confirms both the first spoken reply and a correct answer to a prerecorded 16 kHz question. [Audio exchange results](DESKTOP_AUDIO.md).
7. **ESP32 session request done:** COM26 authenticates with the Worker and validates a temporary session response; local behavior and reboot persistence pass. [ESP32 session results](ESP32_SESSIONS.md).
8. **ESP32 direct setup done:** COM26 verifies Google's WebSocket upgrade, setup acknowledgment and clean closure with a temporary token. Six consecutive final checks and the user's local behavior check pass. [ESP32 Gemini results](ESP32_GEMINI_SETUP.md).
9. **ESP32 speaker output done:** a manual fixed greeting plays through the existing speaker and returns to READY; the user confirms clear speech at normal speed. Repeated output and device-side Wi-Fi-loss recovery pass. [Speaker checkpoint](ESP32_VOICE.md).
10. **ESP32 microphone question/answer verified:** reviewed calibration supports the four-second window and capture shift 15. A real spoken question received the correct audible answer; upload, exact playback, normal closure, READY and the user's confirmation pass. [Question results and repeat procedure](ESP32_QUESTIONS.md).
11. **Armed wake integration verified:** three consecutive physical wake/question/answer/READY cycles pass, with complete technical results and correct clear answers confirmed by the user. The Desktop tester remains available. Next add follow-up listening and idle-session behavior. [Wake results and repeat test](ESP32_WAKE.md). [Roadmap status](ROADMAP_STATUS.md).

Google currently lists free-tier Gemini 3.8 Live input/output, and the user reports a Free-tier project. The successful session check proves access at this checkpoint, not remaining quota. Review the project usage view before longer tests. The Worker handles small control requests; the intended voice stream runs directly between the ESP32 and Gemini. [Session results and limitations](GEMINI_SESSIONS.md).
