#pragma once

#include <cstdint>

enum class LocalCompanionState {
    Idle,
    WakeDetected,
    Listening,
    Speaking,
};

// Local cycle control. LISTENING waits for a capture result supplied by the
// caller; its deadline is a watchdog, never a successful completion. The main
// task owns audio/display effects and validates capture request IDs.
class LocalCompanionCycle {
public:
    static constexpr std::int64_t kWakeDurationUs = 100000;
    static constexpr std::int64_t kListeningTimeoutUs = 4000000;
    static constexpr std::int64_t kSpeakingDurationUs = 800000;

    explicit constexpr LocalCompanionCycle(std::int64_t listening_timeout_us = kListeningTimeoutUs)
        : listening_timeout_us_(listening_timeout_us) {}

    constexpr LocalCompanionState state() const { return state_; }
    constexpr std::uint32_t started() const { return started_; }
    constexpr std::uint32_t completed() const { return completed_; }
    constexpr std::uint32_t aborted() const { return aborted_; }
    constexpr std::int64_t deadline_us() const { return deadline_us_; }

    constexpr bool BeginWake(std::int64_t now_us) {
        if (state_ != LocalCompanionState::Idle) {
            return false;
        }
        state_ = LocalCompanionState::WakeDetected;
        deadline_us_ = now_us + kWakeDurationUs;
        ++started_;
        return true;
    }

    // Advance at most one state. A late tick starts the next state's full dwell
    // from its actual entry time rather than skipping feedback to catch up.
    constexpr bool Tick(std::int64_t now_us) {
        if (state_ == LocalCompanionState::Idle || now_us < deadline_us_) {
            return false;
        }
        switch (state_) {
            case LocalCompanionState::WakeDetected:
                state_ = LocalCompanionState::Listening;
                deadline_us_ = now_us + listening_timeout_us_;
                return true;
            case LocalCompanionState::Listening:
                return Abort();
            case LocalCompanionState::Speaking:
                state_ = LocalCompanionState::Idle;
                deadline_us_ = 0;
                ++completed_;
                return true;
            case LocalCompanionState::Idle:
                return false;
        }
        return false;
    }

    // The caller also checks request ID and successful PCM sample count.
    constexpr bool CaptureFinished(std::int64_t now_us) {
        if (state_ != LocalCompanionState::Listening
            || now_us < deadline_us_ - listening_timeout_us_ || now_us >= deadline_us_) {
            return false;
        }
        state_ = LocalCompanionState::Speaking;
        deadline_us_ = now_us + kSpeakingDurationUs;
        return true;
    }

    // Failed/cancelled cycles return to IDLE without claiming completion.
    constexpr bool Abort() {
        if (state_ == LocalCompanionState::Idle) {
            return false;
        }
        state_ = LocalCompanionState::Idle;
        deadline_us_ = 0;
        ++aborted_;
        return true;
    }

    // Restore a fresh controller, including its counters.
    constexpr void Reset() {
        state_ = LocalCompanionState::Idle;
        deadline_us_ = 0;
        started_ = 0;
        completed_ = 0;
        aborted_ = 0;
    }

    static constexpr const char* StateName(LocalCompanionState state) {
        switch (state) {
            case LocalCompanionState::Idle: return "IDLE";
            case LocalCompanionState::WakeDetected: return "WAKE_DETECTED";
            case LocalCompanionState::Listening: return "LISTENING";
            case LocalCompanionState::Speaking: return "SPEAKING";
        }
        return "INVALID";
    }

private:
    std::int64_t listening_timeout_us_;
    LocalCompanionState state_ = LocalCompanionState::Idle;
    std::int64_t deadline_us_ = 0;
    std::uint32_t started_ = 0;
    std::uint32_t completed_ = 0;
    std::uint32_t aborted_ = 0;
};

namespace local_companion_detail {
// This small contract also runs when compiled by the ESP32 toolchain.
constexpr bool CaptureWatchdogAndRetryInvariant() {
    LocalCompanionCycle cycle;
    if (cycle.CaptureFinished(0) || cycle.Abort() || !cycle.BeginWake(0) || cycle.BeginWake(1)) {
        return false;
    }
    if (cycle.Tick(99999) || !cycle.Tick(100000) || cycle.state() != LocalCompanionState::Listening) {
        return false;
    }
    // Time alone cannot turn LISTENING into a successful speaking state.
    if (cycle.Tick(2100000) || cycle.CaptureFinished(4100000) || !cycle.Tick(4100000)) {
        return false;
    }
    if (cycle.state() != LocalCompanionState::Idle || cycle.completed() != 0 || cycle.aborted() != 1) {
        return false;
    }
    if (!cycle.BeginWake(5000000) || !cycle.Tick(5100000) || !cycle.CaptureFinished(7100000)) {
        return false;
    }
    if (cycle.Tick(7899999) || !cycle.Tick(7900000)) {
        return false;
    }
    return cycle.state() == LocalCompanionState::Idle && cycle.started() == 2
        && cycle.completed() == 1 && cycle.aborted() == 1;
}
}  // namespace local_companion_detail

static_assert(local_companion_detail::CaptureWatchdogAndRetryInvariant(),
    "Local capture watchdog/retry invariants failed");
