#pragma once
#include <algorithm>
#include <cstdint>

namespace companion_followup {
inline constexpr unsigned kMaxTurns = 2;
inline constexpr unsigned kFrameMs = 20, kFrameSamples = 320, kFrames = 200;
inline constexpr std::int64_t kOwnerLeaseUs = 10000000, kSessionLimitUs = 120000000;
// Require sustained local VAD evidence, not one isolated positive frame.
class SpeechGate {
public:
    constexpr bool Feed(bool speech) {
        if (frames_ == kFrames) return false;
        ++frames_; if (speech) ++speech_;
        run_ = speech ? run_ + 1 : 0;
        longest_ = std::max(longest_, run_); return true;
    }
    constexpr bool valid() const { return frames_ == kFrames; }
    constexpr bool speech() const { return valid() && speech_ >= 10 && longest_ >= 5; }
    constexpr unsigned frames() const { return frames_; }
    constexpr unsigned positives() const { return speech_; }
    constexpr unsigned longest() const { return longest_; }
private:
    unsigned frames_ = 0, speech_ = 0, run_ = 0, longest_ = 0;
};
constexpr bool CanUploadFollowup(unsigned completed_turns, const SpeechGate& gate) {
    return completed_turns > 0 && completed_turns < kMaxTurns && gate.speech();
}
}  // namespace companion_followup
