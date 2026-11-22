# Cloudflare and Gemini free-tier usage

Assessment dated **2026-11-21**. Both services are on their **Free tiers**, as confirmed by the project owner. This page describes allowance consumption, not paid-plan costs. Cloudflare numbers are measured analytics; Gemini numbers are estimates.

## How this robot consumes cloud resources

A fresh conversation normally makes one Cloudflare Worker request and one Durable Object admission call to obtain a temporary Gemini credential. Voice audio travels directly between the ESP32 and Gemini. Follow-up questions reuse the Gemini connection without another Cloudflare session request.

The normal firmware permits up to eight answers, 30 seconds idle, or two minutes total per connection. Question capture and reply playback are each bounded to ten seconds. Local wake listening outside a cloud session does not contact Gemini. Quiet input is filtered locally, but an open Gemini connection's provider-side listening usage has not been measured.

The shared Worker admission limit is ten authenticated control requests per rolling minute. This is not an account-wide quota or billing cap: health checks, rejected requests and diagnostics also consume Worker requests.

## Measured Cloudflare usage

Analytics window: **November 14-21, ending November 21 at approximately 3:35 p.m. Pacific**.

| Meter | Last seven days | Last 24 hours |
| --- | ---: | ---: |
| Worker requests | 344 | 2 |
| Worker CPU time | 0.706328 seconds | 0.006253 seconds |
| Worker invocation errors | 0 | 0 |
| Worker response-body data | 59,081 bytes | 718 bytes |

Seven-day Durable Object totals were **168 requests**, **2.33036 GB-seconds**, **609 SQLite rows read**, and **333 rows written**. Peak stored data was **12 KiB**. CPU, memory and fatal error counters were zero.

The busiest measured UTC day, November 18, had **229 Worker requests**. That is **0.229%** of the Free allowance of **100,000 requests/day**. The seven-day average was approximately **49 requests/day**; daily allowances are not pooled across the week.

| Relevant Free allowance | Limit |
| --- | ---: |
| Worker requests | 100,000/day |
| Worker CPU | 10 ms per invocation |
| Durable Object requests | 100,000/day |
| Durable Object duration | 13,000 GB-seconds/day |
| SQLite rows read | 5 million/day |
| SQLite rows written | 100,000/day |
| SQLite storage | 5 GB |

At 100 fresh conversations/day, session setup would use approximately **0.1%** of the Worker request allowance. At 1,000/day, it would use approximately **1%**, before diagnostics and other traffic. Current usage provides no capacity-based reason to upgrade Cloudflare.

These figures came from Cloudflare's analytics API using existing authorization. Adaptive analytics are not invoice line items, and zero invocation errors does not mean every HTTP response was successful. Subrequests include Cloudflare service calls and must not be treated as a Gemini conversation count.

## Estimated Gemini usage

The configured model is **`gemini-3.8-live`**. For development during **November 17-21**, the working ballpark is:

| Quantity | Estimate |
| --- | ---: |
| Completed question-and-answer exchanges | 100-200, midpoint about 150 |
| Uploaded and generated speech combined | 10-30 minutes |
| New audio tokens | Approximately 15,000-40,000 |
| Processed tokens allowing for context and overhead | Roughly 50,000-100,000; potentially higher |

At least **62 completed replies** were identified in distinct saved test batches. Additional greeting tests, fixtures, transport experiments and unrecorded human conversations support the broader estimate. Some credential requests only tested token issuance or connection setup and generated no speech. The local records are incomplete, so these ranges are not account totals or measured quota consumption.

Google documents approximately **25 native audio tokens per second**, or **1,500 per minute**. The following examples count speech and retained audio history only:

| Example | Estimated processed audio tokens |
| --- | ---: |
| Fresh five-second question and five-second reply | 250 |
| Four such exchanges in one conversation | 2,500 |
| 100 exchanges grouped into four-turn conversations | 62,500 |

For the four-turn example, audio input processing is approximately `125 + 375 + 625 + 875 = 2,000` tokens, with 500 newly generated output tokens. Earlier question and reply audio becomes input history on later turns. System instructions, transcripts, reasoning and any metered connected listening add usage. These are illustrative calculations, not readings from Gemini's quota counter.

## What the free allowance means

Google's public documentation does not give one universal numeric Free quota for Gemini 3.8 Live. Limits depend on the project and account status. Published quota dimensions include requests per minute, **input** tokens per minute, and applicable daily limits; daily request quotas reset at midnight Pacific. Total input-plus-output tokens must not be compared directly with an input-only TPM limit.

An exact percentage remaining or a date when compute will run out cannot be inferred from these estimates. The relevant limits replenish on their applicable schedules; no lifetime compute balance has been identified for this project.

For ordinary development, **20-50 short exchanges/day** is a provisional planning target, roughly **5,000-30,000 processed tokens/day** with short conversations and modest overhead. It is not a verified Google entitlement. The same number of exchanges can consume more tokens when grouped into longer conversations, and concentrated testing can encounter short-window limits.

The firmware recognizes but does not retain Gemini's `usageMetadata`. Recording it per session, including input/output modality breakdowns, would replace these estimates with useful measured token totals. No firmware changes, quota changes or service upgrades were made for this assessment.

## Sources

Official documentation checked on November 21, 2026:

- [Cloudflare Workers allowances](https://developers.cloudflare.com/workers/platform/pricing/)
- [Cloudflare Durable Objects allowances](https://developers.cloudflare.com/durable-objects/platform/pricing/)
- [Cloudflare Workers analytics](https://developers.cloudflare.com/workers/observability/metrics-and-analytics/)
- [Gemini Live token and context accounting](https://aistudio.google.com/docs/live-api/best-practices#pricing-billing)
- [Gemini project rate limits](https://aistudio.google.com/docs/rate-limits)
- [Gemini usage metadata](https://aistudio.google.com/docs/live-api/capabilities#token-count)

Local evidence includes the performance audit, distinct test batches, and selected/warm/human voice reports. Raw logs remain outside version control. See [PERFORMANCE.md](PERFORMANCE.md), [GEMINI_SESSIONS.md](GEMINI_SESSIONS.md), and [RATE_LIMITS.md](RATE_LIMITS.md).
