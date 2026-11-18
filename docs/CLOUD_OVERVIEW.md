# Cloudflare and Gemini: the next stage

The local hardware interaction has a successful physical follow-up. Hosted health, device authentication, shared request limits, Gemini temporary-token issuance and a desktop Gemini connection are verified. [Run the Worker](CLOUD_WORKER.md) or [repeat the Gemini session check](GEMINI_SESSIONS.md). No API key is needed for the public health check.

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
6. **Connection done:** Gemini accepts the desktop WebSocket setup. Next verify one spoken response, then a short audio-input exchange.
7. Connect the ESP32 only after those checks pass.

Google currently lists free-tier Gemini 3.8 Live input/output, and the user reports a Free-tier project. The successful session check proves access at this checkpoint, not remaining quota. Review the project usage view before longer tests. The Worker handles small control requests; the intended voice stream runs directly between the ESP32 and Gemini. [Session results and limitations](GEMINI_SESSIONS.md).
