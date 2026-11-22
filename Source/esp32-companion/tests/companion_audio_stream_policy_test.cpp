#include "../main/audio/stream_buffer_policy.h"
#include <array>

namespace {
using namespace companion_audio;

constexpr bool PrebufferAndExactQueueAccounting() {
    PlaybackPrefix prefix(16, 6, 4);
    if (prefix.Next(4, false).action != PlaybackAction::Wait) return false;
    const auto first = prefix.Next(8, false);
    if (first.action != PlaybackAction::Ready || first.offset != 0 || first.samples != 4) return false;
    if (prefix.Commit(3) || !prefix.Commit(4) || prefix.Commit(4)) return false;
    const auto second = prefix.Next(8, false);
    return second.action == PlaybackAction::Ready && second.offset == 4 && second.samples == 4 &&
        prefix.Commit(4) && prefix.Next(8, false).action == PlaybackAction::Wait &&
        prefix.Next(8, true).action == PlaybackAction::Complete;
}
constexpr bool PartialFinishedReplyDoesNotWaitForPrebuffer() {
    PlaybackPrefix prefix(16, 12, 4);
    const auto first = prefix.Next(3, true);
    return first.action == PlaybackAction::Ready && first.offset == 0 && first.samples == 3 &&
        prefix.Commit(3) && prefix.Next(3, true).action == PlaybackAction::Complete && prefix.consumed() == 3;
}
constexpr bool WaitForFullQuantumUntilEof() {
    PlaybackPrefix prefix(16, 4, 4);
    if (prefix.Next(4, false).action != PlaybackAction::Ready || !prefix.Commit(4)) return false;
    if (prefix.Next(6, false).action != PlaybackAction::Wait) return false;
    const auto last = prefix.Next(6, true);
    return last.action == PlaybackAction::Ready && last.offset == 4 && last.samples == 2 && prefix.Commit(2) &&
        prefix.Next(6, true).action == PlaybackAction::Complete;
}
constexpr bool PublishedPrefixCannotRegress() {
    PlaybackPrefix prefix(16, 4, 4);
    if (prefix.Next(8, false).action != PlaybackAction::Ready) return false;
    return prefix.Next(7, false).action == PlaybackAction::Invalid;
}
constexpr bool EofFreezesPrefix() {
    PlaybackPrefix prefix(16, 4, 4);
    if (prefix.Next(4, true).action != PlaybackAction::Ready || !prefix.Commit(4)) return false;
    return prefix.Next(8, true).action == PlaybackAction::Invalid &&
        prefix.Next(4, false).action == PlaybackAction::Invalid;
}
constexpr bool QuietFramesNeverPublish() {
    std::array<std::int16_t, 8> pcm{}, pre_roll{};
    constexpr std::int16_t quiet[] = {1, 2};
    QuestionPublisher input(pcm.data(), pcm.size(), pre_roll.data(), 4);
    for (unsigned i = 0; i < 200; ++i)
        if (!input.Feed(quiet, 2, false) || input.published() || input.speech_started()) return false;
    input.Seal();
    return !input.Feed(quiet, 2, true) && pcm == std::array<std::int16_t, 8>{};
}
constexpr bool PrerollWrapRetainsOrderedLeadingAudio() {
    std::array<std::int16_t, 8> pcm{}, pre_roll{};
    constexpr std::int16_t first[] = {1, 2}, second[] = {3, 4}, third[] = {5, 6}, fourth[] = {7, 8};
    QuestionPublisher input(pcm.data(), pcm.size(), pre_roll.data(), 4);
    if (!input.Feed(first, 2, false) || !input.Feed(second, 2, false) || !input.Feed(third, 2, true)) return false;
    if (input.published() != 4 || pcm[0] != 3 || pcm[1] != 4 || pcm[2] != 5 || pcm[3] != 6) return false;
    if (!input.Feed(fourth, 2, true) || input.published() != 6) return false;
    return pcm[0] == 3 && pcm[1] == 4 && pcm[2] == 5 && pcm[3] == 6 && pcm[4] == 7 && pcm[5] == 8;
}
constexpr bool CapacityFailureKeepsPublishedPcmUntouched() {
    std::array<std::int16_t, 4> pcm{}, pre_roll{};
    constexpr std::int16_t first[] = {1, 2, 3, 4}, extra[] = {5};
    QuestionPublisher input(pcm.data(), pcm.size(), pre_roll.data(), 4);
    if (!input.Feed(first, 4, true)) return false;
    return !input.Feed(extra, 1, true) && input.published() == 4 && pcm == std::array<std::int16_t, 4>{1, 2, 3, 4};
}
constexpr bool SpeechAcceptanceCannotBeRevokedOrWrittenAfterSeal() {
    std::array<std::int16_t, 4> pcm{}, pre_roll{};
    constexpr std::int16_t samples[] = {1, 2};
    QuestionPublisher input(pcm.data(), pcm.size(), pre_roll.data(), 4);
    if (!input.Feed(samples, 2, true) || input.Feed(samples, 2, false)) return false;
    input.Seal();
    return !input.Feed(samples, 2, true) && input.published() == 2;
}

static_assert(PrebufferAndExactQueueAccounting());
static_assert(PartialFinishedReplyDoesNotWaitForPrebuffer());
static_assert(WaitForFullQuantumUntilEof());
static_assert(PublishedPrefixCannotRegress());
static_assert(EofFreezesPrefix());
static_assert(PlaybackPrefix(16).Next(17, false).action == PlaybackAction::Invalid);
static_assert(PlaybackPrefix(16).Next(0, true).action == PlaybackAction::Invalid);
static_assert(PlaybackPrefix(0).Next(0, false).action == PlaybackAction::Invalid);
static_assert(PlaybackPrefix(16, 4, 0).Next(4, false).action == PlaybackAction::Invalid);
static_assert(QuietFramesNeverPublish());
static_assert(PrerollWrapRetainsOrderedLeadingAudio());
static_assert(CapacityFailureKeepsPublishedPcmUntouched());
static_assert(SpeechAcceptanceCannotBeRevokedOrWrittenAfterSeal());
static_assert(!QuestionPublisher(nullptr, 0, nullptr, 0).valid());
}  // namespace
