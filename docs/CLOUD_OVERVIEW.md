# Cloudflare and Gemini: the next stage

The local hardware interaction has a successful physical follow-up, and the first Worker health endpoint is verified both locally and on its public HTTPS URL. [Run the Worker and see the next checkpoint](CLOUD_WORKER.md). No API key is needed for the current health check.

A **Cloudflare Worker** is a small program hosted by Cloudflare that responds to requests. We will use it as the robot's authentication/session endpoint; there is no virtual server for you to maintain. [Cloudflare Workers overview](https://developers.cloudflare.com/workers/)

**Gemini Live** will handle the spoken conversation. Our intended flow is:

```text
Robot -> Worker: authenticate and request a session
Worker -> Gemini: obtain a temporary access token
Worker -> Robot: return temporary token
Robot <-> Gemini Live: voice over a secure WebSocket
```

The permanent Gemini API key will be stored as a **Worker secret**, accessible to the server program rather than embedded in the robot firmware. When we reach setup, enter it through the local/dashboard secret workflow; keep it out of chat and Git. [Cloudflare secrets](https://developers.cloudflare.com/workers/configuration/secrets/)

Google documents the backend-token/client-WebSocket pattern for Gemini Live. Temporary tokens can restrict a client's session access. This feature is currently preview, so we will verify the supported model, API version, token settings and quota when implementing it. [Gemini Live ephemeral tokens](https://aistudio.google.com/docs/live-api/ephemeral-tokens)

We will verify the cloud stage in small steps:

1. **Done locally:** create a Worker with a simple health response and test its success/error routes.
2. **Done hosted:** the user deployed the health-only service and its HTTPS success/error responses passed verification.
3. **Done hosted:** device authentication rejects missing/wrong credentials and accepts the correct one.
4. Add request limits before enabling provider calls.
5. Store the Gemini key as a Worker secret and test temporary-token issuance.
6. Prove a desktop Gemini connection and voice response before adding embedded transport.
7. Connect the ESP32 only after those checks pass.

We will check current free-tier eligibility/limits and model availability at that stage; owning an API key does not by itself establish quota or cost. The Worker handles small control requests; the intended voice stream runs directly between the ESP32 and Gemini.
