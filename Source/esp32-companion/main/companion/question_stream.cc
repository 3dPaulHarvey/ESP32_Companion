#include "question_stream.h"
#include <esp_heap_caps.h>
#include <mbedtls/platform_util.h>
#if COMPANION_QUESTION_FIXTURE
#include <cstring>
#include <new>
#endif

namespace companion_question {

QuestionStream::QuestionStream(std::uint32_t request) : request_(request) {
    pcm_ = static_cast<std::int16_t*>(heap_caps_calloc(
        kCapacitySamples + kPreRollSamples, sizeof(std::int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

QuestionStream::~QuestionStream() {
    if (pcm_) {
        mbedtls_platform_zeroize(pcm_, (kCapacitySamples + kPreRollSamples) * sizeof(*pcm_));
        heap_caps_free(pcm_);
    }
}

#if COMPANION_QUESTION_FIXTURE
std::shared_ptr<QuestionStream> QuestionStream::FromFixture(std::uint32_t request, const std::int16_t* pcm, std::size_t samples) {
    if (!request || !pcm || !samples || samples > kCapacitySamples) return nullptr;
    std::shared_ptr<QuestionStream> stream;
    try {
        stream = std::make_shared<QuestionStream>(request);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    if (!stream->ready()) return nullptr;
    std::memcpy(stream->pcm_, pcm, samples * sizeof(*pcm));
    return stream;
}
#endif

}  // namespace companion_question
