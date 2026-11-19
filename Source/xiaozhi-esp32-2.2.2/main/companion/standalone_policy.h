#pragma once
#include <cstdint>

namespace companion_standalone {
class Policy {
public:
    enum class Decision { Cloud, Paused, Offline, Unconfigured, Cooldown, RetryWindow };
    static constexpr std::int64_t kControlLeaseUs = 10000000, kFailureCooldownUs = 30000000;
    constexpr void Pause(std::int64_t now) { control_until_ = now + kControlLeaseUs; }
    constexpr bool Heartbeat(std::int64_t now) {
        if (!control_until_ || now >= control_until_) return false;
        control_until_ = now + kControlLeaseUs; return true;
    }
    constexpr bool Expire(std::int64_t now) {
        if (!control_until_ || now < control_until_) return false;
        Resume(); return true;
    }
    constexpr void Resume() { control_until_ = 0; }
    constexpr bool paused() const { return control_until_ != 0; }
    // A diagnostic client's w/o commands are temporary; restore the robot's
    // prior network preference when control ends or its lease expires.
    constexpr void NetworkCommand(bool wanted) { if (!paused()) wifi_wanted_ = wanted; }
    constexpr bool wifi_wanted() const { return wifi_wanted_; }
    constexpr void Finished(std::int64_t now, bool ok) { cooldown_until_ = ok ? 0 : now + kFailureCooldownUs; }
    constexpr Decision Decide(std::int64_t now, bool connected, bool configured, bool retry_open) const {
        return paused() ? Decision::Paused : (!connected || !wifi_wanted_) ? Decision::Offline : !configured ? Decision::Unconfigured :
            now < cooldown_until_ ? Decision::Cooldown : !retry_open ? Decision::RetryWindow : Decision::Cloud;
    }
    constexpr unsigned cooldown_ms(std::int64_t now) const {
        return now < cooldown_until_ ? static_cast<unsigned>((cooldown_until_ - now + 999) / 1000) : 0;
    }
private:
    bool wifi_wanted_ = true;
    std::int64_t control_until_ = 0, cooldown_until_ = 0;
};
constexpr const char* DecisionName(Policy::Decision decision) {
    switch (decision) {
        case Policy::Decision::Cloud: return "cloud";
        case Policy::Decision::Paused: return "control_paused";
        case Policy::Decision::Offline: return "offline";
        case Policy::Decision::Unconfigured: return "credential_required";
        case Policy::Decision::Cooldown: return "cooldown";
        case Policy::Decision::RetryWindow: return "retry_window";
    }
    return "invalid";
}
}  // namespace companion_standalone
