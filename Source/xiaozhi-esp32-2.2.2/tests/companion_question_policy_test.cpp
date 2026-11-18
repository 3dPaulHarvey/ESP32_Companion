#include "../main/companion/question_audio_policy.h"
#include <array>
#include <string_view>
using namespace companion_question;
namespace {
constexpr bool Frame(std::int16_t a, std::int16_t b, std::string_view expected, bool single = false) {
    const std::int16_t pcm[] = {a, b};
    std::array<char, kFrameCapacity> out{};
    const auto size = EncodeFrame(pcm, single ? 1 : 2, out.data(), out.size());
    return size && std::string_view(out.data(), sizeof(kPrefix) - 1) == kPrefix &&
        std::string_view(out.data() + sizeof(kPrefix) - 1, expected.size()) == expected &&
        std::string_view(out.data() + size - (sizeof(kSuffix) - 1)) == kSuffix;
}
constexpr bool Bounds() {
    std::array<std::int16_t, kChunkSamples + 1> pcm{};
    std::array<char, kFrameCapacity> out{};
    const auto size = EncodeFrame(pcm.data(), kChunkSamples, out.data(), out.size());
    return size > 0 && size < out.size() && !EncodeFrame(pcm.data(), kChunkSamples, out.data(), size) &&
        !EncodeFrame(pcm.data(), 0, out.data(), out.size()) &&
        !EncodeFrame(pcm.data(), kChunkSamples + 1, out.data(), out.size()) &&
        !EncodeFrame(nullptr, 1, out.data(), out.size());
}
static_assert(Frame(1, 0, "AQA=", true));
static_assert(Frame(0x1234, -1, "NBL//w=="));
static_assert(Frame(-32768, 32767, "AID/fw=="));
static_assert(Bounds());
static_assert(kSamples * 2 == 128000);
static_assert(kSamples / kChunkSamples == 100);
static_assert((kSamples / kChunkSamples) * kChunkMs == 4000);
static_assert(std::string_view(kEnd) == "{\"realtimeInput\":{\"audioStreamEnd\":true}}");
}
