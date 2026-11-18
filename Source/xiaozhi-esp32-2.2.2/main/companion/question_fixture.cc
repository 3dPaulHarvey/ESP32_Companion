#include "question_fixture.h"
#include <cstring>
#include <new>

extern const std::uint8_t fixture_start[] asm("_binary_robot_question_public_fixture_pcm_start");
extern const std::uint8_t fixture_end[] asm("_binary_robot_question_public_fixture_pcm_end");

std::unique_ptr<companion_question::Pcm> MakeQuestionFixture() {
    const auto size = reinterpret_cast<std::uintptr_t>(fixture_end) - reinterpret_cast<std::uintptr_t>(fixture_start);
    if (size != companion_question::kSamples * sizeof(std::int16_t)) return nullptr;
    auto* data = static_cast<std::int16_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) return nullptr;
    std::memcpy(data, fixture_start, size);
    auto pcm = std::unique_ptr<companion_question::Pcm>(new (std::nothrow) companion_question::Pcm(0, data));
    if (!pcm) { mbedtls_platform_zeroize(data, size); heap_caps_free(data); }
    return pcm;
}
