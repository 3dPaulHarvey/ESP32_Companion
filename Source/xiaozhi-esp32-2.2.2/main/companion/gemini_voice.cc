#include "gemini_voice.h"
#include <cmath>
#include <new>
#include <esp_heap_caps.h>
#include <mbedtls/platform_util.h>

namespace CompanionGeminiVoice {
Reply::Reply() {
    pcm_ = static_cast<std::uint8_t*>(heap_caps_calloc(1, companion_voice::kAudioLimit, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    void* document = heap_caps_malloc(sizeof(companion_voice::JsonDocument), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (document) json_ = new (document) companion_voice::JsonDocument;
}
Reply::~Reply() {
    if (pcm_) { mbedtls_platform_zeroize(pcm_, companion_voice::kAudioLimit); heap_caps_free(pcm_); }
    if (json_) { json_->~JsonDocument(); mbedtls_platform_zeroize(json_, sizeof(*json_)); heap_caps_free(json_); }
}
bool Reply::Consume(std::string_view message, bool input_mode) {
    if (!ready()) { reason = "allocation_failed"; return false; }
    if (!json_->Parse(message)) { reason = "invalid_or_complex_json"; return false; }
    return companion_voice::Consume(*json_, pcm_, companion_voice::kAudioLimit, state, reason, input_mode);
}
void Reply::Clear() {
    if (pcm_) mbedtls_platform_zeroize(pcm_, companion_voice::kAudioLimit);
    if (json_) {
        json_->~JsonDocument(); mbedtls_platform_zeroize(json_, sizeof(*json_));
        new (json_) companion_voice::JsonDocument;
    }
    state = {}; reason = "unavailable";
}
unsigned Reply::rms() const {
    return state.bytes ? static_cast<unsigned>(std::sqrt(static_cast<double>(state.squares) / (state.bytes / 2))) : 0;
}
}  // namespace CompanionGeminiVoice
