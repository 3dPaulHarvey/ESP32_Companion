#pragma once
#include <algorithm>
#include <cstdint>

namespace companion_wake {
// Explicitly armed, one-use authorization. Heartbeats cannot extend the
// one-minute waiting window or revive an expired/disconnected owner.
class Arm {
public:
    enum class Expiry { None, Lease, Window };
    static constexpr std::int64_t kLeaseUs = 10000000, kWindowUs = 60000000;
    constexpr bool ArmOnce(std::int64_t now) {
        if (waiting_) return false;
        waiting_ = true; window_ = now + kWindowUs; lease_ = now + kLeaseUs;
        return true;
    }
    constexpr bool Heartbeat(std::int64_t now) {
        if (Expire(now) != Expiry::None || !waiting_) return false;
        lease_ = std::min(window_, now + kLeaseUs); return true;
    }
    constexpr Expiry Expire(std::int64_t now) {
        if (!waiting_) return Expiry::None;
        const auto reason = now >= window_ ? Expiry::Window : now >= lease_ ? Expiry::Lease : Expiry::None;
        if (reason != Expiry::None) Disarm();
        return reason;
    }
    constexpr bool Consume(std::int64_t now) {
        if (Expire(now) != Expiry::None || !waiting_) return false;
        Disarm(); return true;
    }
    constexpr void Disarm() { waiting_ = false; lease_ = window_ = 0; }
    constexpr bool waiting() const { return waiting_; }
private:
    bool waiting_ = false;
    std::int64_t window_ = 0, lease_ = 0;
};
}  // namespace companion_wake
