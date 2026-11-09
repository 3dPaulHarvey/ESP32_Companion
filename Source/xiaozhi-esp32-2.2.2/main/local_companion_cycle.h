#pragma once

#include <cstdint>

enum class LocalCompanionState {
    Idle,
    WakeDetected,
    Listening,
    Speaking,
};

// A timed local state/UI rehearsal. This controller owns no audio, display,
// network, or RTOS resources; the main task performs each state's effects.
class LocalCompanionCycle {
public:
    static constexpr std::int64_t kWakeDurationUs = 100000;
    static constexpr std::int64_t kListeningDurationUs = 400000;
    static constexpr std::int64_t kSpeakingDurationUs = 800000;

    constexpr LocalCompanionCycle() = default;

    constexpr LocalCompanionState state() const { return state_; }
    constexpr std::uint32_t started() const { return started_; }
    constexpr std::uint32_t completed() const { return completed_; }
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
                deadline_us_ = now_us + kListeningDurationUs;
                return true;
            case LocalCompanionState::Listening:
                state_ = LocalCompanionState::Speaking;
                deadline_us_ = now_us + kSpeakingDurationUs;
                return true;
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

    // Restore a fresh controller, including its counters.
    constexpr void Reset() {
        state_ = LocalCompanionState::Idle;
        deadline_us_ = 0;
        started_ = 0;
        completed_ = 0;
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
    LocalCompanionState state_ = LocalCompanionState::Idle;
    std::int64_t deadline_us_ = 0;
    std::uint32_t started_ = 0;
    std::uint32_t completed_ = 0;
};

namespace local_companion_detail {
// These checks also run when the header is compiled by the ESP32 toolchain.
constexpr bool CoreInvariants() {
    LocalCompanionCycle cycle;
    if (cycle.state() != LocalCompanionState::Idle || cycle.started() != 0 || cycle.completed() != 0) {
        return false;
    }
    if (cycle.Tick(0) || !cycle.BeginWake(0) || cycle.BeginWake(1)) {
        return false;
    }
    if (cycle.Tick(99999) || !cycle.Tick(100000) || cycle.state() != LocalCompanionState::Listening) {
        return false;
    }
    if (cycle.Tick(499999) || !cycle.Tick(500000) || cycle.state() != LocalCompanionState::Speaking) {
        return false;
    }
    if (cycle.completed() != 0 || cycle.Tick(1299999) || !cycle.Tick(1300000)) {
        return false;
    }
    if (cycle.state() != LocalCompanionState::Idle || cycle.started() != 1 || cycle.completed() != 1) {
        return false;
    }
    cycle.Reset();
    if (!cycle.BeginWake(0) || !cycle.Tick(1000000)) {
        return false;
    }
    return cycle.state() == LocalCompanionState::Listening
        && cycle.deadline_us() == 1400000 && !cycle.Tick(1000000) && cycle.completed() == 0;
}

constexpr bool TenCompleteCycles() {
    LocalCompanionCycle cycle;
    for (std::uint32_t i = 0; i < 10; ++i) {
        const std::int64_t start = static_cast<std::int64_t>(i) * 2000000;
        if (!cycle.BeginWake(start) || !cycle.Tick(start + 100000)
            || !cycle.Tick(start + 500000) || !cycle.Tick(start + 1300000)) {
            return false;
        }
        if (cycle.state() != LocalCompanionState::Idle || cycle.started() != i + 1 || cycle.completed() != i + 1) {
            return false;
        }
    }
    return true;
}
}  // namespace local_companion_detail

static_assert(local_companion_detail::CoreInvariants(), "Local companion transition invariants failed");
static_assert(local_companion_detail::TenCompleteCycles(), "Local companion repeated-cycle invariants failed");
