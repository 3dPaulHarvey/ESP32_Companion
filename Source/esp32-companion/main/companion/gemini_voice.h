#pragma once

#include "gemini_voice_policy.h"

namespace CompanionGeminiVoice {
class Reply {
public:
    Reply();
    ~Reply();
    Reply(const Reply&) = delete;
    Reply& operator=(const Reply&) = delete;
    bool ready() const { return pcm_ && json_; }
    bool Consume(std::string_view message, bool input_mode = false, bool live_mode = false);
    void Clear();
    const int16_t* samples() const { return reinterpret_cast<const int16_t*>(pcm_); }
    unsigned rms() const;
    companion_voice::AudioState state{};
    const char* reason = "unavailable";
private:
    std::uint8_t* pcm_ = nullptr;
    companion_voice::JsonDocument* json_ = nullptr;
};
}  // namespace CompanionGeminiVoice
