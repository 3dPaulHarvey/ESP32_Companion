#include "session_probe.h"
#include "session_policy.h"

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
enum class Kind { Session, Wrong, Missing };
const char* KindName(Kind kind) { return kind == Kind::Session ? "session" : kind == Kind::Wrong ? "wrong" : "missing"; }
std::atomic<bool> busy{false};
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
    char* data = static_cast<char*>(heap_caps_calloc(1, companion_session::kBodyLimit + 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
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

Result Perform(Kind kind) {
    Result result;
    if (!WifiManager::GetInstance().IsConnected()) { result.reason = "wifi_required"; return result; }
    Credential credential;
    if (kind != Kind::Missing && !LoadCredential(credential)) { result.reason = "credential_required"; return result; }
    if (!EnsureTime()) { result.reason = "time_sync_failed"; return result; }
    ESP_LOGI(kTag, "TIME ready certificate_dates=verified");
    ResponseBody body;
    if (!body.data) { result.reason = "allocation_failed"; return result; }
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
    const int expected = kind == Kind::Session ? 200 : 401;
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
    if (kind == Kind::Session) {
        if (!companion_session::ValidateSession(payload, static_cast<int64_t>(std::time(nullptr)) * 1000,
                std::string_view(credential.value, 64), result.metadata)) {
            result.reason = "invalid_session_response"; return result;
        }
    } else if (payload != "{\"error\":\"unauthorized\"}" || std::strcmp(headers.challenge, "Bearer realm=\"esp32-companion\"") != 0) {
        result.reason = "invalid_rejection_response"; return result;
    }
    result.ok = true;
    result.reason = "verified";
    return result;
}

void Request(Kind kind) {
    if (esp_timer_get_time() < retry_until_us.load()) { ESP_LOGW(kTag, "REQUEST_REJECTED reason=retry_window"); return; }
    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true)) { ESP_LOGW(kTag, "REQUEST_REJECTED reason=busy"); return; }
    pending_kind = kind;
    const auto created = xTaskCreate([](void*) {
        const auto kind = pending_kind;
        const auto started = esp_timer_get_time();
        ESP_LOGI(kTag, "REQUEST_BEGIN kind=%s no_audio_upload=1", KindName(kind));
        const auto result = Perform(kind);
        ESP_LOGI(kTag, "RESULT kind=%s ok=%d http=%d reason=%s temporary_bytes=%u start_in_s=%d expires_in_s=%d elapsed_ms=%lu discarded=1",
            KindName(kind), result.ok, result.http_status, result.reason,
            static_cast<unsigned>(result.metadata.temporary_bytes), result.metadata.start_in_seconds,
            result.metadata.expires_in_seconds, static_cast<unsigned long>((esp_timer_get_time() - started) / 1000));
        busy.store(false);
        vTaskDelete(nullptr);
    }, "session_probe", 12288, nullptr, 1, nullptr);
    if (created != pdPASS) { busy.store(false); ESP_LOGE(kTag, "REQUEST_REJECTED reason=task_allocation_failed"); }
}
}  // namespace

namespace CompanionSessionProbe {
void Initialize() {
    ESP_LOGI(kTag, "MODE manual-session-probe no_audio_upload=1 automatic_requests=0");
    ESP_LOGI(kTag, "COMMANDS g=session-check b=wrong-credential-check u=missing-credential-check provision=private-host-tool");
    PrintStatus();
}
void PrintStatus() {
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    Credential credential;
    ESP_LOGI(kTag, "PROTOCOL version=1 mac=%02x:%02x:%02x:%02x:%02x:%02x configured=%d busy=%d",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], LoadCredential(credential), busy.load());
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
    return false;
}
}  // namespace CompanionSessionProbe
