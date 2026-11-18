#include "gemini_setup.h"
#include "gemini_setup_policy.h"

#include <algorithm>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_transport_ssl.h>
#include <esp_transport_ws.h>
#include <mbedtls/platform_util.h>

#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_MBEDTLS_HAVE_TIME_DATE || CONFIG_ESP_TLS_INSECURE
#error "Gemini setup requires CA, hostname and certificate-date verification."
#endif
#if !COMPANION_WS_LOGS_DISABLED || CONFIG_LOG_MAXIMUM_LEVEL > 3
#error "Credential-bearing WebSocket transport logs must be compiled out."
#endif
static_assert(CONFIG_WS_BUFFER_SIZE >= companion_gemini::kPathCapacity + 1024,
    "WebSocket upgrade buffer must fit the largest encoded temporary token.");

namespace {
constexpr char kTag[] = "CompanionGemini";
constexpr char kHost[] = "generativelanguage.googleapis.com";
constexpr int kTransportTimeoutMs = 8000;

struct PrivateBuffer {
    char* data;
    std::size_t capacity;
    explicit PrivateBuffer(std::size_t size) :
        data(static_cast<char*>(heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))), capacity(size) {}
    ~PrivateBuffer() { if (data) { mbedtls_platform_zeroize(data, capacity); heap_caps_free(data); } }
    PrivateBuffer(const PrivateBuffer&) = delete;
    PrivateBuffer& operator=(const PrivateBuffer&) = delete;
};
struct Scratch {
    char data[512]{};
    char control[125]{};
    ~Scratch() { mbedtls_platform_zeroize(this, sizeof(*this)); }
};
struct Transport {
    esp_transport_handle_t tls = nullptr;
    esp_transport_handle_t ws = nullptr;
    ~Transport() {
        if (ws) { esp_transport_close(ws); esp_transport_destroy(ws); }
        if (tls) esp_transport_destroy(tls);
    }
};

// The IDF WebSocket transport masks and restores its send buffer in place.
// All payloads passed here are writable, including setup and close messages.
int Send(esp_transport_handle_t ws, unsigned opcode, char* data, std::size_t size, int timeout_ms) {
    return esp_transport_ws_send_raw(ws, static_cast<ws_transport_opcodes_t>(opcode | 0x80),
        data, static_cast<int>(size), timeout_ms);
}

CompanionGeminiSetup::Result VerifyConnection(std::string_view temporary) {
    CompanionGeminiSetup::Result result;
    PrivateBuffer path(companion_gemini::kPathCapacity);
    PrivateBuffer message(companion_gemini::kMessageLimit + 1);
    Scratch scratch;
    if (!path.data || !message.data) { result.reason = "allocation_failed"; return result; }
    std::size_t path_size = 0;
    if (!companion_gemini::EncodePath(temporary, path.data, path.capacity, path_size)) {
        result.reason = "invalid_temporary_credential"; return result;
    }
    Transport transport;
    transport.tls = esp_transport_ssl_init();
    if (!transport.tls) { result.reason = "tls_init_failed"; return result; }
    esp_transport_ssl_crt_bundle_attach(transport.tls, esp_crt_bundle_attach);
    esp_transport_ssl_set_common_name(transport.tls, kHost);
    transport.ws = esp_transport_ws_init(transport.tls);
    if (!transport.ws) { result.reason = "websocket_init_failed"; return result; }
    esp_transport_ws_config_t config{};
    config.ws_path = path.data;
    config.user_agent = "ESP32-Companion-Gemini-Setup/0.1";
    config.propagate_control_frames = true;
    if (esp_transport_ws_set_config(transport.ws, &config) != ESP_OK) {
        result.reason = "websocket_config_failed"; return result;
    }
    ESP_LOGI(kTag, "CONNECT_BEGIN no_question=1 no_audio_upload=1");
    const int connected = esp_transport_connect(transport.ws, kHost, 443, kTransportTimeoutMs);
    result.upgrade_http = esp_transport_ws_get_upgrade_request_status(transport.ws);
    // The transport reports redirect statuses as positive connect results.
    // Accept only a verified 101; never follow its redirection URI.
    if (connected != 0 || result.upgrade_http != 101) { result.reason = "websocket_upgrade_failed"; return result; }
    ESP_LOGI(kTag, "UPGRADE_VERIFIED http=101");
    char setup[] = "{\"setup\":{\"model\":\"models/gemini-3.8-live\"}}";
    if (Send(transport.ws, 1, setup, sizeof(setup) - 1, 3000) != sizeof(setup) - 1) {
        result.reason = "setup_send_failed"; return result;
    }
    mbedtls_platform_zeroize(setup, sizeof(setup));
    companion_gemini::FrameTracker frames;
    std::size_t post_close_bytes = 0;
    auto deadline_us = esp_timer_get_time() + 20000000;
    while (esp_timer_get_time() < deadline_us) {
        const int timeout_ms = static_cast<int>(std::min<int64_t>(1000, (deadline_us - esp_timer_get_time()) / 1000));
        if (timeout_ms <= 0) break;
        const int count = esp_transport_read(transport.ws, scratch.data, sizeof(scratch.data), timeout_ms);
        if (count < 0) { result.reason = "websocket_read_failed"; return result; }
        const auto opcode = esp_transport_ws_get_read_opcode(transport.ws);
        if (opcode == WS_TRANSPORT_OPCODES_NONE) {
            if (count != 0) { result.reason = "invalid_frame_state"; return result; }
            continue;
        }
        const int length = esp_transport_ws_get_read_payload_len(transport.ws);
        if (!count && length > 0) { result.reason = "partial_frame_timeout"; return result; }
        if (length < 0 || (!frames.active() && !frames.Begin(opcode,
                esp_transport_ws_get_fin_flag(transport.ws), static_cast<size_t>(length))) ||
            frames.opcode() != static_cast<unsigned>(opcode)) {
            result.reason = "invalid_or_oversized_frame"; return result;
        }
        const auto offset = frames.control() ? frames.control_size() : frames.message_size();
        const auto capacity = frames.control() ? sizeof(scratch.control) : companion_gemini::kMessageLimit;
        if (static_cast<size_t>(count) > capacity - offset) { result.reason = "oversized_message"; return result; }
        if (count) std::memcpy((frames.control() ? scratch.control : message.data) + offset, scratch.data, count);
        const auto piece = frames.Append(count);
        if (piece == companion_gemini::PieceResult::Invalid) { result.reason = "invalid_frame_length"; return result; }
        if (piece == companion_gemini::PieceResult::Control) {
            const auto size = frames.control_size();
            if (frames.opcode() == 9) {
                if (Send(transport.ws, 10, scratch.control, size, 1000) != static_cast<int>(size)) {
                    result.reason = "pong_send_failed"; return result;
                }
            } else if (frames.opcode() == 8) {
                result.close_code = size >= 2 ? (static_cast<unsigned char>(scratch.control[0]) << 8) |
                    static_cast<unsigned char>(scratch.control[1]) : 0;
                if (!result.close_sent) {
                    Send(transport.ws, 8, scratch.control, size, 1000);
                    result.reason = "closed_before_setup"; return result;
                }
                result.close_ack = true;
                result.ok = result.setup_ack && (result.close_code == 0 || result.close_code == 1000 || result.close_code == 1001);
                result.reason = result.ok ? "verified" : "unexpected_close_code";
                return result;
            }
        } else if (piece == companion_gemini::PieceResult::Message) {
            if (result.close_sent) {
                // A server data message can already be in flight when our
                // Close is sent. Drain it without interpreting its contents;
                // only a close control frame can complete this check. Keep
                // both the aggregate drain size and close deadline bounded.
                post_close_bytes += frames.message_size();
                if (post_close_bytes > companion_gemini::kMessageLimit) {
                    result.reason = "oversized_close_drain"; return result;
                }
                ESP_LOGI(kTag, "CLOSE_DRAIN bytes=%u", static_cast<unsigned>(frames.message_size()));
                frames.ResetMessage();
                continue;
            }
            if (!companion_gemini::IsSetupComplete(std::string_view(message.data, frames.message_size()))) {
                result.reason = "unexpected_setup_response"; return result;
            }
            result.setup_ack = true;
            result.ack_bytes = frames.message_size();
            ESP_LOGI(kTag, "SETUP_ACK received=1 bytes=%u", result.ack_bytes);
            frames.ResetMessage();
            char close_payload[] = {0x03, static_cast<char>(0xe8)};
            if (Send(transport.ws, 8, close_payload, sizeof(close_payload), 1000) != sizeof(close_payload)) {
                result.reason = "close_send_failed"; return result;
            }
            result.close_sent = true;
            deadline_us = esp_timer_get_time() + 2000000;
        }
    }
    result.reason = result.close_sent ? "close_timeout" : "setup_timeout";
    return result;
}
}  // namespace

namespace CompanionGeminiSetup {
void Initialize() {
    // SDK transport_ws has an ERROR path that includes its upgrade request.
    // CMake compiles transport logs out; disable this tag at runtime as well.
    esp_log_level_set("transport_ws", ESP_LOG_NONE);
}
Result Verify(std::string_view temporary) {
    const auto started = esp_timer_get_time();
    auto result = VerifyConnection(temporary);
    // All transports and owned private buffers have already been released.
    result.elapsed_ms = static_cast<uint32_t>((esp_timer_get_time() - started) / 1000);
    return result;
}
}  // namespace CompanionGeminiSetup
