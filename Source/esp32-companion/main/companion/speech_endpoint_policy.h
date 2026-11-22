#pragma once

#include <algorithm>
#include <cstddef>

namespace companion_endpoint {
inline constexpr unsigned kSampleRate = 16000;
inline constexpr unsigned kFrameMs = 20, kFrameSamples = kSampleRate * kFrameMs / 1000;
inline constexpr unsigned kQuietMs = 4000, kSilenceMs = 600, kMaximumDurationMs = 10000;
inline constexpr unsigned kPreRollMs = 320, kPreRollFrames = kPreRollMs / kFrameMs;
inline constexpr unsigned kMinimumSpeechFrames = 10, kMinimumRunFrames = 5;
inline constexpr std::size_t kMaximumSamples = kSampleRate * kMaximumDurationMs / 1000;

enum class Decision { Listening, SpeechStarted, SpeechEnded, Quiet, Limit };

// Feed exactly one VAD decision per captured 20 ms frame, after microphone
// warmup. Speech acceptance uses the existing sustained-speech thresholds.
// Accepted speech remains accepted while pauses are measured. All limits are
// relative to capture start, so background noise cannot extend capture forever.
class Policy {
public:
    constexpr Decision Feed(bool speech) {
        if (finished()) return decision_;
        ++frames_;
        if (speech) {
            ++speech_frames_;
            ++run_;
            longest_ = std::max(longest_, run_);
            silence_frames_ = 0;
        } else {
            run_ = 0;
            ++silence_frames_;
        }
        const bool started = !speech_started_ && speech_frames_ >= kMinimumSpeechFrames && longest_ >= kMinimumRunFrames;
        speech_started_ = speech_started_ || started;
        // A valid speech start on the last quiet-wait frame is still accepted.
        if (!speech_started_ && duration_ms() >= kQuietMs) decision_ = Decision::Quiet;
        else if (speech_started_ && silence_frames_ * kFrameMs >= kSilenceMs) decision_ = Decision::SpeechEnded;
        else if (duration_ms() >= kMaximumDurationMs) decision_ = Decision::Limit;
        else decision_ = started ? Decision::SpeechStarted : Decision::Listening;
        return decision_;
    }
    constexpr bool speech_started() const { return speech_started_; }
    constexpr bool finished() const {
        return decision_ == Decision::SpeechEnded || decision_ == Decision::Quiet || decision_ == Decision::Limit;
    }
    constexpr Decision decision() const { return decision_; }
    constexpr unsigned frames() const { return frames_; }
    constexpr unsigned speech_frames() const { return speech_frames_; }
    constexpr unsigned longest() const { return longest_; }
    constexpr unsigned silence_frames() const { return silence_frames_; }
    constexpr unsigned duration_ms() const { return frames_ * kFrameMs; }
private:
    unsigned frames_ = 0, speech_frames_ = 0, run_ = 0, longest_ = 0, silence_frames_ = 0;
    bool speech_started_ = false;
    Decision decision_ = Decision::Listening;
};
}  // namespace companion_endpoint
