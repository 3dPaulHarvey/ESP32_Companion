#pragma once

#include <algorithm>
#include <cstdint>

namespace companion_live_session {
inline constexpr std::int64_t kIdleWindowUs = 30000000, kSessionLimitUs = 120000000;
inline constexpr unsigned kMaxTurns = 8;

enum class Status { Ready, TurnActive, IdleExpired, SessionExpired, TurnLimit, InvalidClock };

// A connection may be warm between wakes, but neither an idle wake nor a
// completed reply extends its absolute lifetime. The transport remains the
// owner of cancellation, connectivity and provider expiry handling.
class Policy {
public:
    explicit constexpr Policy(std::int64_t started_us) : started_us_(started_us) {}
    constexpr Status status(std::int64_t now_us) const {
        if (started_us_ < 0 || now_us < started_us_ || now_us < last_event_us_) return Status::InvalidClock;
        if (now_us - started_us_ >= kSessionLimitUs) return Status::SessionExpired;
        if (turns_ >= kMaxTurns) return Status::TurnLimit;
        if (turn_active_) return Status::TurnActive;
        if (idle_until_us_ && now_us >= idle_until_us_) return Status::IdleExpired;
        return Status::Ready;
    }
    constexpr bool BeginTurn(std::int64_t now_us) {
        if (status(now_us) != Status::Ready) return false;
        turn_active_ = true;
        idle_until_us_ = 0;
        last_event_us_ = now_us;
        return true;
    }
    constexpr bool CompleteTurn(std::int64_t now_us) {
        if (status(now_us) != Status::TurnActive) return false;
        ++turns_;
        turn_active_ = false;
        last_event_us_ = now_us;
        idle_until_us_ = now_us + std::min(kIdleWindowUs, kSessionLimitUs - (now_us - started_us_));
        return true;
    }
    constexpr unsigned turns() const { return turns_; }
    constexpr bool turn_active() const { return turn_active_; }
    constexpr std::int64_t idle_deadline_us() const { return idle_until_us_; }
    constexpr std::int64_t remaining_us(std::int64_t now_us) const {
        const auto state = status(now_us);
        if (state != Status::Ready && state != Status::TurnActive) return 0;
        const auto session_left = kSessionLimitUs - (now_us - started_us_);
        return idle_until_us_ ? std::min(session_left, idle_until_us_ - now_us) : session_left;
    }
private:
    std::int64_t started_us_ = 0, last_event_us_ = 0, idle_until_us_ = 0;
    unsigned turns_ = 0;
    bool turn_active_ = false;
};
}  // namespace companion_live_session
