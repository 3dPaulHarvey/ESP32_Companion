#include "../main/companion/speech_endpoint_policy.h"

namespace {
using namespace companion_endpoint;

constexpr Policy Frames(unsigned count, bool speech) {
    Policy policy;
    for (unsigned i = 0; i < count; ++i) policy.Feed(speech);
    return policy;
}
constexpr bool QuietEndsExactly() {
    auto policy = Frames(kQuietMs / kFrameMs - 1, false);
    if (policy.finished() || policy.speech_started()) return false;
    if (policy.Feed(false) != Decision::Quiet || policy.duration_ms() != kQuietMs) return false;
    return policy.Feed(true) == Decision::Quiet && policy.frames() == kQuietMs / kFrameMs && !policy.speech_started();
}
constexpr bool NoiseDoesNotStartSpeech() {
    Policy policy;
    for (unsigned i = 0; i < kQuietMs / kFrameMs; ++i) policy.Feed(i % 2 == 0);
    return policy.decision() == Decision::Quiet && !policy.speech_started() && policy.longest() == 1;
}
constexpr bool SpeechStartsOnce() {
    auto policy = Frames(kMinimumSpeechFrames - 1, true);
    if (policy.speech_started() || policy.finished()) return false;
    if (policy.Feed(true) != Decision::SpeechStarted || !policy.speech_started() || policy.finished()) return false;
    return policy.Feed(true) == Decision::Listening && policy.speech_started();
}
constexpr bool PausesDoNotEndEarly() {
    auto policy = Frames(kMinimumSpeechFrames, true);
    for (unsigned i = 0; i < kSilenceMs / kFrameMs - 1; ++i) {
        if (policy.Feed(false) != Decision::Listening) return false;
    }
    if (policy.Feed(true) != Decision::Listening || policy.silence_frames() != 0) return false;
    for (unsigned i = 0; i < kSilenceMs / kFrameMs - 1; ++i) policy.Feed(false);
    if (policy.Feed(false) != Decision::SpeechEnded || !policy.speech_started()) return false;
    const auto frames = policy.frames();
    return policy.Feed(true) == Decision::SpeechEnded && policy.frames() == frames;
}
constexpr bool LateSpeechIsAccepted() {
    auto policy = Frames(kQuietMs / kFrameMs - kMinimumSpeechFrames, false);
    for (unsigned i = 0; i < kMinimumSpeechFrames - 1; ++i) policy.Feed(true);
    return policy.Feed(true) == Decision::SpeechStarted && policy.duration_ms() == kQuietMs && !policy.finished();
}
constexpr bool AcceptedSpeechHasAbsoluteBound() {
    auto policy = Frames(kMaximumDurationMs / kFrameMs - 1, true);
    if (policy.finished()) return false;
    if (policy.Feed(true) != Decision::Limit || policy.duration_ms() != kMaximumDurationMs || !policy.speech_started()) return false;
    return policy.Feed(false) == Decision::Limit && policy.frames() * kFrameSamples == kMaximumSamples;
}
constexpr bool MinimumRunStillRequired() {
    Policy policy;
    for (unsigned i = 0; i < kMinimumSpeechFrames * 2; ++i) policy.Feed(i % 2 == 0);
    if (policy.speech_started()) return false;
    for (unsigned i = 0; i < kMinimumRunFrames - 1; ++i) policy.Feed(true);
    return !policy.speech_started() && policy.Feed(true) == Decision::SpeechStarted;
}
static_assert(kFrameSamples == 320 && kPreRollFrames == 16 && kMaximumSamples == 160000);
static_assert(!Policy{}.finished() && !Policy{}.speech_started() && Policy{}.duration_ms() == 0);
static_assert(QuietEndsExactly());
static_assert(NoiseDoesNotStartSpeech());
static_assert(SpeechStartsOnce());
static_assert(PausesDoNotEndEarly());
static_assert(LateSpeechIsAccepted());
static_assert(AcceptedSpeechHasAbsoluteBound());
static_assert(MinimumRunStillRequired());
}  // namespace
