#include "session_probe.h"
#include "session_policy.h"
#if CONFIG_COMPANION_GEMINI_SETUP_PROBE
#include "gemini_setup.h"
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
#include "gemini_voice.h"
#include "application.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>
#include <mbedtls/platform_util.h>
#include <nvs.h>
#include <wifi_manager.h>
#include <freertos/task.h>

#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_MBEDTLS_HAVE_TIME_DATE || CONFIG_ESP_TLS_INSECURE
#error "The session probe requires certificate bundle, validity-date checks and secure TLS."
#endif
#if CONFIG_LOG_MAXIMUM_LEVEL > 3
#error "The credential-bearing probe requires INFO-or-lower compiled logging."
#endif

namespace {
constexpr char kTag[] = "CompanionSession";
constexpr char kUrl[] = "https://esp32-companion-control.stiener.workers.dev/device/session";
constexpr char kNamespace[] = "companion";
constexpr char kCredentialKey[] = "device_auth";
enum class Kind { Session, Wrong, Missing, Gemini, Voice, Question };
const char* KindName(Kind kind) {
    return kind == Kind::Session ? "session" : kind == Kind::Wrong ? "wrong" : kind == Kind::Missing ? "missing" : kind == Kind::Gemini ? "gemini" : kind == Kind::Question ? "question" : "voice";
}
std::atomic<bool> busy{false};
#if CONFIG_COMPANION_MICROPHONE_PREP
std::atomic<bool> calibration_guard{false};
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
std::atomic<bool> question_capture_guard{false}, question_cancelled{false};
std::uint32_t pending_capture_id = 0;
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
std::atomic<CompanionSessionProbe::VoiceStage> voice_stage{CompanionSessionProbe::VoiceStage::Idle};
#endif
std::atomic<std::int64_t> retry_until_us{0};
Kind pending_kind = Kind::Session;
companion_session::CredentialFrame provision_frame;

struct Credential {
    char value[65]{};
    ~Credential() { mbedtls_platform_zeroize(value, sizeof(value)); }
};
bool LoadCredential(Credential& output) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = sizeof(output.value);
    const auto error = nvs_get_str(handle, kCredentialKey, output.value, &length);
    nvs_close(handle);
    return error == ESP_OK && length == 65 && output.value[64] == 0 &&
        companion_session::ValidCredential(std::string_view(output.value, 64));
}
bool StoreCredential(const char* value) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    auto error = nvs_set_str(handle, kCredentialKey, value);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

struct Headers {
    char content_type[64]{};
    char cache[24]{};
    char revision[40]{};
    char challenge[64]{};
    char retry[12]{};
    bool invalid = false;
    template <size_t N> void Copy(char (&target)[N], const char* value) {
        const size_t length = std::strlen(value);
        if (target[0] || length >= N) { invalid = true; return; }
        std::memcpy(target, value, length + 1);
    }
    bool Valid() const {
        return !invalid && (std::strcmp(content_type, "application/json") == 0 ||
            std::strcmp(content_type, "application/json; charset=utf-8") == 0) &&
            std::strcmp(cache, "no-store") == 0 && std::strcmp(revision, "gemini-session-v1") == 0;
    }
};
esp_err_t OnHttpEvent(esp_http_client_event_t* event) {
    if (event->event_id != HTTP_EVENT_ON_HEADER || !event->user_data || !event->header_key || !event->header_value) return ESP_OK;
    auto& headers = *static_cast<Headers*>(event->user_data);
    const auto key = event->header_key;
    if (!strcasecmp(key, "Content-Type")) headers.Copy(headers.content_type, event->header_value);
    else if (!strcasecmp(key, "Cache-Control")) headers.Copy(headers.cache, event->header_value);
    else if (!strcasecmp(key, "X-Companion-Revision")) headers.Copy(headers.revision, event->header_value);
    else if (!strcasecmp(key, "WWW-Authenticate")) headers.Copy(headers.challenge, event->header_value);
    else if (!strcasecmp(key, "Retry-After")) headers.Copy(headers.retry, event->header_value);
    return ESP_OK;
}

struct HttpClient {
    esp_http_client_handle_t handle = nullptr;
    ~HttpClient() {
        if (handle) {
            esp_http_client_delete_header(handle, "Authorization");
            esp_http_client_cleanup(handle);
        }
    }
};
struct ResponseBody {
    char* data = nullptr;
    bool Allocate() {
        data = static_cast<char*>(heap_caps_calloc(1, companion_session::kBodyLimit + 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        return data != nullptr;
    }
    ~ResponseBody() {
        if (data) { mbedtls_platform_zeroize(data, companion_session::kBodyLimit + 2); heap_caps_free(data); }
    }
};
struct Authorization {
    char value[72]{};
    ~Authorization() { mbedtls_platform_zeroize(value, sizeof(value)); }
};
struct Result {
    bool ok = false;
    int http_status = 0;
    const char* reason = "unavailable";
    companion_session::SessionMetadata metadata{};
#if CONFIG_COMPANION_GEMINI_SETUP_PROBE
    CompanionGeminiSetup::Result gemini{};
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    companion_voice::AudioState audio{};
    uint32_t played_samples = 0, playback_ms = 0;
    unsigned rms = 0;
#endif
};

bool EnsureTime() {
    if (std::time(nullptr) >= 1735689600) return true;
    ESP_LOGI(kTag, "TIME syncing deadline_ms=15000");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
        ESP_SNTP_SERVER_LIST("time.google.com", "pool.ntp.org"));
    if (esp_netif_sntp_init(&config) != ESP_OK) return false;
    const auto error = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    esp_netif_sntp_deinit();
    return error == ESP_OK && std::time(nullptr) >= 1735689600;
}

Result FetchSession(Kind kind, ResponseBody& body, std::string_view& temporary) {
    Result result;
    if (!WifiManager::GetInstance().IsConnected()) { result.reason = "wifi_required"; return result; }
    Credential credential;
    if (kind != Kind::Missing && !LoadCredential(credential)) { result.reason = "credential_required"; return result; }
    if (!EnsureTime()) { result.reason = "time_sync_failed"; return result; }
    ESP_LOGI(kTag, "TIME ready certificate_dates=verified");
    if (!body.Allocate()) { result.reason = "allocation_failed"; return result; }
    Headers headers;
    esp_http_client_config_t config{};
    config.url = kUrl;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.disable_auto_redirect = true;
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
    config.event_handler = OnHttpEvent;
    config.user_data = &headers;
    config.user_agent = "ESP32-Companion-Device-Check/0.1";
    HttpClient client;
    client.handle = esp_http_client_init(&config);
    if (!client.handle) { result.reason = "client_init_failed"; return result; }
    Authorization authorization;
    if (kind != Kind::Missing) {
        std::memcpy(authorization.value, "Bearer ", 7);
        std::memcpy(authorization.value + 7, credential.value, 64);
        if (kind == Kind::Wrong) authorization.value[7] = authorization.value[7] == '0' ? '1' : '0';
        if (esp_http_client_set_header(client.handle, "Authorization", authorization.value) != ESP_OK) {
            result.reason = "header_failed"; return result;
        }
    }
    if (esp_http_client_set_header(client.handle, "Content-Length", "0") != ESP_OK) {
        result.reason = "header_failed"; return result;
    }
    const auto deadline_us = esp_timer_get_time() + 15000000;
    if (esp_http_client_open(client.handle, 0) != ESP_OK) { result.reason = "https_open_failed"; return result; }
    const auto length = esp_http_client_fetch_headers(client.handle);
    result.http_status = esp_http_client_get_status_code(client.handle);
    if (length < 0 || esp_timer_get_time() >= deadline_us) { result.reason = "headers_failed"; return result; }
    if (result.http_status == 429) {
        int seconds = 0;
        for (const char* c = headers.retry; *c && seconds <= 60; ++c) {
            if (*c < '0' || *c > '9') { seconds = 0; break; }
            seconds = seconds * 10 + *c - '0';
        }
        if (seconds >= 1 && seconds <= 60) retry_until_us.store(esp_timer_get_time() + seconds * 1000000LL);
        result.reason = "rate_limited"; return result;
    }
    const int expected = kind == Kind::Wrong || kind == Kind::Missing ? 401 : 200;
    if (result.http_status != expected) { result.reason = "unexpected_http_status"; return result; }
    if (!headers.Valid()) { result.reason = "unexpected_headers"; return result; }
    if (length > static_cast<int64_t>(companion_session::kBodyLimit)) { result.reason = "oversized_response"; return result; }
    size_t used = 0;
    // fetch_headers may already have cached the entire body. Always drain
    // esp_http_client_read before trusting its transport-complete flag.
    while (true) {
        const auto remaining_ms = (deadline_us - esp_timer_get_time()) / 1000;
        if (remaining_ms <= 0) { result.reason = "response_timeout"; return result; }
        esp_http_client_set_timeout_ms(client.handle, static_cast<int>(std::min<int64_t>(1000, remaining_ms)));
        const auto capacity = companion_session::kBodyLimit + 1 - used;
        const int count = esp_http_client_read(client.handle, body.data + used, std::min<size_t>(1024, capacity));
        if (count < 0) { result.reason = "response_read_failed"; return result; }
        used += count;
        if (used > companion_session::kBodyLimit) { result.reason = "oversized_response"; return result; }
        if (!count) {
            if (esp_http_client_is_complete_data_received(client.handle)) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    const std::string_view payload(body.data, used);
    if (kind == Kind::Session || kind == Kind::Gemini || kind == Kind::Voice || kind == Kind::Question) {
        if (!companion_session::ValidateSession(payload, static_cast<int64_t>(std::time(nullptr)) * 1000,
                std::string_view(credential.value, 64), result.metadata, &temporary)) {
            result.reason = "invalid_session_response"; return result;
        }
    } else if (payload != "{\"error\":\"unauthorized\"}" || std::strcmp(headers.challenge, "Bearer realm=\"esp32-companion\"") != 0) {
        result.reason = "invalid_rejection_response"; return result;
    }
    result.ok = true;
    result.reason = "verified";
    return result;
}

Result Perform(Kind kind, [[maybe_unused]] std::uint32_t capture_id) {
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    auto question = kind == Kind::Question ? Application::GetInstance().GetAudioService().TakeQuestionAudio(capture_id) : nullptr;
    if (kind == Kind::Question && (!question || !question->data() || question_cancelled.load())) {
        Result missing; missing.reason = question_cancelled.load() ? "cancelled" : "capture_unavailable"; return missing;
    }
#endif
    ResponseBody body;
    std::string_view temporary;
    // HTTP client, its TLS context, device credential and Authorization buffer
    // are destroyed before a Gemini transport is allocated.
    auto result = FetchSession(kind, body, temporary);
#if CONFIG_COMPANION_GEMINI_SETUP_PROBE
    if (result.ok && kind == Kind::Gemini) {
        if (result.metadata.start_in_seconds < 20) { result.ok = false; result.reason = "session_window_too_short"; return result; }
        result.gemini = CompanionGeminiSetup::Verify(temporary);
        result.ok = result.gemini.ok;
        result.reason = result.gemini.reason;
    }
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    if (result.ok && (kind == Kind::Voice || kind == Kind::Question)) {
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
        if (kind == Kind::Question && question_cancelled.load()) { result.ok = false; result.reason = "cancelled"; return result; }
#endif
        if (result.metadata.start_in_seconds < 20) { result.ok = false; result.reason = "session_window_too_short"; return result; }
        CompanionGeminiVoice::Reply reply;
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
        if (kind == Kind::Question) result.gemini = CompanionGeminiSetup::ReceiveQuestion(temporary, reply, *question);
        else
#endif
        result.gemini = CompanionGeminiSetup::ReceiveGreeting(temporary, reply);
        result.ok = result.gemini.ok;
        result.reason = result.gemini.reason;
        result.audio = reply.state;
        result.rms = reply.rms();
        if (result.ok) {
            // The cloud transport is already closed. Only the established
            // audio output task writes PCM; the UI loop owns expressions.
            voice_stage.store(CompanionSessionProbe::VoiceStage::Speaking);
            const auto started = esp_timer_get_time();
            result.ok = Application::GetInstance().GetAudioService().PlayDiagnosticPcm(
                reply.samples(), reply.state.bytes / sizeof(int16_t), result.played_samples);
            // Keep wake detection disabled while the microphone's DMA history
            // passes the final speaker samples. This is local settling time.
            if (result.ok) vTaskDelay(pdMS_TO_TICKS(150));
            result.playback_ms = static_cast<uint32_t>((esp_timer_get_time() - started) / 1000);
            if (!result.ok) result.reason = "playback_failed";
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
            if (kind == Kind::Question && question_cancelled.load()) { result.ok = false; result.reason = "cancelled"; }
#endif
        }
    }
#endif
    return result;
}

bool Request(Kind kind, [[maybe_unused]] std::uint32_t capture_id = 0) {
#if CONFIG_COMPANION_MICROPHONE_PREP
    if (calibration_guard.load()) { ESP_LOGW(kTag, "REQUEST_REJECTED reason=calibrator_owned"); return false; }
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    if (question_capture_guard.load()) { ESP_LOGW(kTag, "REQUEST_REJECTED reason=question_capture_owned"); return false; }
#endif
    if (esp_timer_get_time() < retry_until_us.load()) { ESP_LOGW(kTag, "REQUEST_REJECTED reason=retry_window"); return false; }
    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true)) { ESP_LOGW(kTag, "REQUEST_REJECTED reason=busy"); return false; }
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    if (kind == Kind::Voice || kind == Kind::Question) voice_stage.store(CompanionSessionProbe::VoiceStage::Connecting);
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    pending_capture_id = capture_id;
    if (kind == Kind::Question) question_cancelled.store(false);
#endif
    pending_kind = kind;
    const auto created = xTaskCreate([](void*) {
        const auto kind = pending_kind;
        const auto started = esp_timer_get_time();
        ESP_LOGI(kTag, "REQUEST_BEGIN kind=%s no_audio_upload=%d", KindName(kind), kind != Kind::Question);
        const auto result = Perform(kind,
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
            pending_capture_id
#else
            0
#endif
        );
#if CONFIG_COMPANION_GEMINI_SETUP_PROBE
        if (kind == Kind::Gemini) {
            ESP_LOGI("CompanionGemini", "RESULT ok=%d upgrade_http=%d setup_ack=%d close_sent=%d close_ack=%d close_code=%u ack_bytes=%u elapsed_ms=%lu cleanup=1 no_question=1 no_audio_upload=1",
                result.gemini.ok, result.gemini.upgrade_http, result.gemini.setup_ack, result.gemini.close_sent,
                result.gemini.close_ack, result.gemini.close_code, result.gemini.ack_bytes,
                static_cast<unsigned long>(result.gemini.elapsed_ms));
        }
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
        if (kind == Kind::Voice || kind == Kind::Question) {
            ESP_LOGI(kind == Kind::Question ? "CompanionQuestion" : "CompanionVoice", "RESULT ok=%d http=%d reason=%s upgrade_http=%d setup_ack=%d prompt_sent=%d turn_complete=%d close_sent=%d close_ack=%d close_code=%u audio_bytes=%u chunks=%u sample_rate=24000 played_samples=%lu duration_ms=%u rms=%u peak=%u clipped=%u playback_ms=%lu elapsed_ms=%lu cleanup=1 no_audio_upload=%d",
                result.ok, result.http_status, result.reason, result.gemini.upgrade_http, result.gemini.setup_ack,
                result.gemini.prompt_sent, result.audio.turn_complete, result.gemini.close_sent, result.gemini.close_ack,
                result.gemini.close_code, static_cast<unsigned>(result.audio.bytes), result.audio.chunks,
                static_cast<unsigned long>(result.played_samples), static_cast<unsigned>(result.audio.bytes / 48),
                result.rms, result.audio.peak, result.audio.clipped, static_cast<unsigned long>(result.playback_ms),
                static_cast<unsigned long>((esp_timer_get_time() - started) / 1000), kind != Kind::Question);
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
            if (kind == Kind::Question) ESP_LOGI("CompanionQuestion", "INPUT bytes=%u chunks=%u stream_ended=%d sample_rate=16000 input_cleared=1",
                result.gemini.input_bytes, result.gemini.input_chunks, result.gemini.input_ended);
#endif
        }
#endif
        ESP_LOGI(kTag, "RESULT kind=%s ok=%d http=%d reason=%s temporary_bytes=%u start_in_s=%d expires_in_s=%d elapsed_ms=%lu discarded=1",
            KindName(kind), result.ok, result.http_status, result.reason,
            static_cast<unsigned>(result.metadata.temporary_bytes), result.metadata.start_in_seconds,
            result.metadata.expires_in_seconds, static_cast<unsigned long>((esp_timer_get_time() - started) / 1000));
        busy.store(false);
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
        if (kind == Kind::Voice || kind == Kind::Question) voice_stage.store(CompanionSessionProbe::VoiceStage::Finished);
#endif
        vTaskDelete(nullptr);
    }, "session_probe", (kind == Kind::Voice || kind == Kind::Question || kind == Kind::Gemini) ? 16384 : 12288, nullptr, 1, nullptr);
    if (created != pdPASS) {
        busy.store(false);
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
        if (kind == Kind::Voice || kind == Kind::Question) voice_stage.store(CompanionSessionProbe::VoiceStage::Idle);
#endif
        ESP_LOGE(kTag, "REQUEST_REJECTED reason=task_allocation_failed");
    }
    return created == pdPASS;
}
}  // namespace

namespace CompanionSessionProbe {
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
void SetQuestionCaptureGuard(bool active) { question_capture_guard.store(active); }
bool RequestQuestion(std::uint32_t capture_id) {
    if (!WifiManager::GetInstance().IsConnected()) { ESP_LOGW("CompanionQuestion", "REQUEST_REJECTED reason=wifi_required"); return false; }
    return Request(Kind::Question, capture_id);
}
void CancelQuestion() { question_cancelled.store(true); }
bool QuestionCancelled() { return question_cancelled.load(); }
#endif
#if CONFIG_COMPANION_MICROPHONE_PREP
bool IsBusy() { return busy.load(); }
void SetCalibrationGuard(bool active) { calibration_guard.store(active); }
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
bool RequestVoice() {
    if (!WifiManager::GetInstance().IsConnected()) { ESP_LOGW("CompanionVoice", "REQUEST_REJECTED reason=wifi_required"); return false; }
    return Request(Kind::Voice);
}
VoiceStage GetVoiceStage() { return voice_stage.load(); }
void AcknowledgeVoice() { voice_stage.store(VoiceStage::Idle); }
#endif
void Initialize() {
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    ESP_LOGI(kTag, "MODE manual-session-probe audio_upload=manual_question_only automatic_requests=0");
#else
    ESP_LOGI(kTag, "MODE manual-session-probe no_audio_upload=1 automatic_requests=0");
#endif
    ESP_LOGI(kTag, "COMMANDS g=session-check b=wrong-credential-check u=missing-credential-check provision=private-host-tool");
#if CONFIG_COMPANION_GEMINI_SETUP_PROBE
    CompanionGeminiSetup::Initialize();
    ESP_LOGI(kTag, "COMMANDS j=direct-Gemini-setup-check");
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    ESP_LOGI("CompanionVoice", "COMMANDS v=fixed-Gemini-greeting no_audio_upload=1");
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    ESP_LOGI("CompanionQuestion", "COMMANDS q=manual-microphone-question x=cancel automatic_requests=0");
#endif
    PrintStatus();
}
void PrintStatus() {
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    Credential credential;
    ESP_LOGI(kTag, "PROTOCOL version=1 mac=%02x:%02x:%02x:%02x:%02x:%02x configured=%d busy=%d",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], LoadCredential(credential), busy.load());
#if CONFIG_COMPANION_GEMINI_SETUP_PROBE
    ESP_LOGI(kTag, "FEATURES gemini_setup=1 automatic_requests=0 audio_upload=0");
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    ESP_LOGI(kTag, "VOICE_FEATURE enabled=1 fixed_greeting=1 automatic_requests=0 audio_upload=0");
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    ESP_LOGI("CompanionQuestion", "FEATURE enabled=1 explicit_start=1 capture_samples=64000 audio_upload=manual_only");
#endif
}
void Poll(std::int64_t now_us) {
    if (provision_frame.Expire(now_us)) ESP_LOGW(kTag, "PROVISION ok=0 reason=frame_timeout");
}
bool HandleSerialByte(char byte, std::int64_t now_us) {
    const auto event = provision_frame.Feed(byte, now_us);
    if (event == companion_session::ProvisionEvent::Ready) {
        const bool saved = !busy.load() && StoreCredential(provision_frame.credential());
        provision_frame.Reset();
        ESP_LOGI(kTag, "PROVISION ok=%d", saved);
        return true;
    }
    if (event == companion_session::ProvisionEvent::Invalid) { ESP_LOGW(kTag, "PROVISION ok=0 reason=invalid_frame"); return true; }
    if (event == companion_session::ProvisionEvent::Receiving) return true;
    if (byte == 'g') { Request(Kind::Session); return true; }
    if (byte == 'b') { Request(Kind::Wrong); return true; }
    if (byte == 'u') { Request(Kind::Missing); return true; }
#if CONFIG_COMPANION_GEMINI_SETUP_PROBE
    if (byte == 'j') { Request(Kind::Gemini); return true; }
#endif
    return false;
}
}  // namespace CompanionSessionProbe
