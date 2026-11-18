#include "../main/companion/gemini_setup_policy.h"

namespace {
using namespace companion_gemini;

constexpr bool PathEscapesCredential() {
    char target[512]{};
    std::size_t used;
    if (!EncodePath("auth_tokens/a+b/c?d&e=f%g", target, sizeof(target), used)) return false;
    return std::string_view(target + kPathPrefix.size()) == "auth_tokens%2Fa%2Bb%2Fc%3Fd%26e%3Df%25g" &&
        used == std::string_view(target).size();
}
constexpr bool PathLimits() {
    char target[512]{};
    std::size_t used;
    return !EncodePath("too-short", target, sizeof(target), used) && used == 0 &&
        !EncodePath("auth_tokens/has space", target, sizeof(target), used) &&
        !EncodePath("auth_tokens/new\nline", target, sizeof(target), used) &&
        !EncodePath("auth_tokens/public-fixture", target, kPathPrefix.size() + 1, used);
}
constexpr bool LargestPath() {
    std::array<char, 8192> token{};
    for (auto& c : token) c = '/';
    std::array<char, kPathCapacity> path{};
    std::size_t used;
    return EncodePath(std::string_view(token.data(), token.size()), path.data(), path.size(), used) &&
        used == kPathPrefix.size() + token.size() * 3 && path[used] == 0;
}
constexpr bool TransportChunks() {
    FrameTracker frames;
    if (!frames.Begin(2, true, 20) || frames.Append(7) != PieceResult::Pending || !frames.active()) return false;
    return frames.Append(13) == PieceResult::Message && !frames.active() && frames.message_size() == 20 &&
        !frames.Begin(2, true, 1);
}
constexpr bool FragmentedWithPing() {
    FrameTracker frames;
    if (!frames.Begin(1, false, 10) || frames.Append(10) != PieceResult::Pending) return false;
    if (!frames.Begin(9, true, 3) || frames.Append(1) != PieceResult::Pending || frames.Append(2) != PieceResult::Control) return false;
    if (frames.message_size() != 10 || frames.control_size() != 3) return false;
    return frames.Begin(0, true, 10) && frames.Append(10) == PieceResult::Message && frames.message_size() == 20;
}
constexpr bool RejectBadFrameOrder() {
    FrameTracker frames;
    if (frames.Begin(0, true, 1) || frames.Begin(7, true, 1)) return false;
    if (!frames.Begin(1, false, 4) || frames.Begin(2, true, 1) || frames.Append(5) != PieceResult::Invalid) return false;
    if (frames.Append(4) != PieceResult::Pending) return false;
    return !frames.Begin(2, true, 1);
}
constexpr bool RejectControlLimits() {
    FrameTracker frames;
    if (frames.Begin(9, false, 0) || frames.Begin(9, true, 126) || frames.Begin(8, true, 1)) return false;
    return frames.Begin(8, true, 0) && frames.Append(0) == PieceResult::Control;
}
constexpr bool BoundWholeMessage() {
    FrameTracker frames;
    if (frames.Begin(1, true, kMessageLimit + 1)) return false;
    if (!frames.Begin(2, false, kMessageLimit) || frames.Append(kMessageLimit) != PieceResult::Pending) return false;
    return !frames.Begin(0, true, 1);
}
constexpr bool ResetBetweenMessages() {
    FrameTracker frames;
    if (!frames.Begin(1, true, 20) || frames.Append(20) != PieceResult::Message) return false;
    frames.ResetMessage();
    return frames.Begin(2, true, 0) && frames.Append(0) == PieceResult::Message && frames.message_size() == 0;
}

static_assert(PathEscapesCredential());
static_assert(PathLimits());
static_assert(LargestPath());
static_assert(IsSetupComplete("{\"setupComplete\":{}}"));
static_assert(IsSetupComplete(" { \"setupComplete\" : { } } \n"));
static_assert(!IsSetupComplete("{\"error\":{\"message\":\"setupComplete\"}}"));
static_assert(!IsSetupComplete("{\"setupComplete\":false}"));
static_assert(!IsSetupComplete("{\"setupComplete\":{\"unexpected\":1}}"));
static_assert(!IsSetupComplete("{\"setupComplete\":{},\"setupComplete\":{}}"));
static_assert(!IsSetupComplete("{\"setupComplete\":{}}garbage"));
static_assert(TransportChunks());
static_assert(FragmentedWithPing());
static_assert(RejectBadFrameOrder());
static_assert(RejectControlLimits());
static_assert(BoundWholeMessage());
static_assert(ResetBetweenMessages());
}  // namespace
