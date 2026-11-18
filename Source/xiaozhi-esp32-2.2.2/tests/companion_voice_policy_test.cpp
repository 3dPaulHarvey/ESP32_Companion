#include "../main/companion/gemini_voice_policy.h"
#include "../main/companion/gemini_setup_policy.h"

namespace {
using namespace companion_voice;
constexpr char kAudio[] = "{\"serverContent\":{\"modelTurn\":{\"role\":\"model\",\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"AQD//w==\"}}]}}}";
constexpr bool JsonAccepts(std::string_view text) { JsonDocument json; return json.Parse(text); }
constexpr bool VoiceAccepts(std::string_view text) {
    JsonDocument json; AudioState state; std::array<std::uint8_t, 16> pcm{}; const char* reason = "none";
    return json.Parse(text) && Consume(json, pcm.data(), pcm.size(), state, reason);
}
constexpr bool OrderedAudioAndCompletion() {
    JsonDocument json; AudioState state; std::array<std::uint8_t, 8> pcm{}; const char* reason = "none";
    if (!json.Parse(kAudio) || !Consume(json, pcm.data(), pcm.size(), state, reason) || state.turn_complete) return false;
    if (state.bytes != 4 || pcm[0] != 1 || pcm[1] != 0 || pcm[2] != 255 || pcm[3] != 255 || state.squares != 2 || state.peak != 1) return false;
    if (!json.Parse(kAudio) || !Consume(json, pcm.data(), pcm.size(), state, reason)) return false;
    if (!json.Parse("{\"serverContent\":{\"generationComplete\":true}}") || !Consume(json, pcm.data(), pcm.size(), state, reason) || state.turn_complete) return false;
    if (!json.Parse("{\"serverContent\":{\"turnComplete\":true}}") || !Consume(json, pcm.data(), pcm.size(), state, reason)) return false;
    return state.turn_complete && state.bytes == 8 && state.chunks == 2 &&
        !Consume(json, pcm.data(), pcm.size(), state, reason);
}
constexpr bool DecodeAccepted(std::string_view text, std::size_t capacity = 16) {
    std::array<std::uint8_t, 16> pcm{}; std::size_t written = 0;
    return DecodePcm(text, pcm.data(), capacity, written);
}
constexpr bool AudioCapacityBound() {
    JsonDocument json; AudioState state; std::array<std::uint8_t, 4> pcm{}; const char* reason = "none";
    return json.Parse(kAudio) && Consume(json, pcm.data(), pcm.size(), state, reason) &&
        !Consume(json, pcm.data(), pcm.size(), state, reason) && state.bytes == 4;
}
constexpr bool TokenBound() {
    std::array<char, 1024> text{}; std::size_t i = 0;
    for (char c : std::string_view("{\"items\":[")) text[i++] = c;
    for (unsigned n = 0; n < 200; ++n) { if (n) text[i++] = ','; text[i++] = '0'; }
    text[i++] = ']'; text[i++] = '}';
    return !JsonAccepts(std::string_view(text.data(), i));
}
constexpr bool VoiceFrameBound() {
    companion_gemini::FrameTracker frames(kMessageLimit);
    return frames.Begin(2, true, kMessageLimit) && frames.Append(kMessageLimit) == companion_gemini::PieceResult::Message;
}
constexpr bool SignedPcmMetrics() {
    JsonDocument json; AudioState state; std::array<std::uint8_t, 4> pcm{}; const char* reason = "none";
    return json.Parse("{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"AID/fw==\"}}]}}}") &&
        Consume(json, pcm.data(), pcm.size(), state, reason) && state.clipped == 2 && state.peak == 32768 && state.squares == 2147418113ULL;
}
constexpr bool ResumptionNoticeIsOnlyMetadata() {
    JsonDocument json; AudioState state; std::array<std::uint8_t, 4> pcm{}; const char* reason = "none";
    return json.Parse("{\"sessionResumptionUpdate\":{\"resumable\":false,\"newHandle\":\"public-fixture\"}}") &&
        Consume(json, pcm.data(), pcm.size(), state, reason) && !state.turn_complete && !state.bytes && !state.chunks;
}
static_assert(JsonAccepts("{\"usageMetadata\":{\"totalTokenCount\":12,\"x\":-1.25e+2},\"serverContent\":{\"outputTranscription\":{\"text\":\"hi\\nthere\\u0021\"}}}"));
static_assert(!JsonAccepts("{\"x\":1,\"x\":2}"));
static_assert(!JsonAccepts("{\"x\":{\"a\":1,\"a\":2}}"));
static_assert(!JsonAccepts("{\"\\u0078\":1}"));
static_assert(!JsonAccepts("{\"x\":[[[[[[[[[[0]]]]]]]]]]}"));
static_assert(!JsonAccepts("{\"x\":01}"));
static_assert(!JsonAccepts("{\"x\":truegarbage}"));
static_assert(!JsonAccepts("{\"x\":\"bad\\q\"}"));
static_assert(!JsonAccepts("{\"x\":1,}"));
static_assert(!JsonAccepts("{\"x\":1}trailing"));
static_assert(TokenBound());
static_assert(DecodeAccepted("AQD//w=="));
static_assert(!DecodeAccepted("AQ=="));
static_assert(!DecodeAccepted("AQD//w==", 3));
static_assert(!DecodeAccepted("AQD//x=="));
static_assert(!DecodeAccepted("AQD_//=="));
static_assert(!DecodeAccepted("AQD//w==AAAA"));
static_assert(OrderedAudioAndCompletion());
static_assert(AudioCapacityBound());
static_assert(VoiceAccepts(kAudio));
static_assert(!VoiceAccepts("{\"toolCall\":{}}"));
static_assert(!VoiceAccepts("{\"error\":{\"message\":\"private\"}}"));
static_assert(!VoiceAccepts("{\"serverContent\":{\"turnComplete\":true}}"));
static_assert(!VoiceAccepts("{\"serverContent\":{\"interrupted\":true}}"));
static_assert(!VoiceAccepts("{\"serverContent\":{\"turnComplete\":\"true\"}}"));
static_assert(!VoiceAccepts("{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"AQD//w==\"}}]}}}"));
static_assert(!VoiceAccepts("{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"AQD\\/\\/w==\"}}]}}}"));
static_assert(VoiceFrameBound());
static_assert(SignedPcmMetrics());
static_assert(ResumptionNoticeIsOnlyMetadata());
static_assert(!VoiceAccepts("{\"sessionResumptionUpdate\":true}"));
static_assert(JsonAccepts(kGreeting));
}  // namespace
