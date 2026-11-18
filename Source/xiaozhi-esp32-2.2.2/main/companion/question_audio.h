#pragma once
#include "question_audio_policy.h"
#include <esp_heap_caps.h>
#include <mbedtls/platform_util.h>

namespace companion_question {
// Created by the microphone worker only for an explicit question. Ownership
// moves directly to the session task; neither UI nor host receives PCM.
class Pcm {
public:
    Pcm(std::uint32_t request, std::int16_t* data) : request_(request), data_(data) {}
    ~Pcm() { Clear(); }
    Pcm(const Pcm&) = delete;
    Pcm& operator=(const Pcm&) = delete;
    const std::int16_t* data() const { return data_; }
    std::uint32_t request() const { return request_; }
    void Clear() {
        if (data_) { mbedtls_platform_zeroize(data_, kSamples * sizeof(*data_)); heap_caps_free(data_); data_ = nullptr; }
    }
private:
    std::uint32_t request_;
    std::int16_t* data_;
};
}  // namespace companion_question
