#pragma once
#include <cstdint>

// A disconnected/crashed calibrator cannot leave local wake permanently paused.
class MicrophoneCalibrationLease {
public:
    static constexpr std::int64_t kTimeoutUs = 10000000;
    constexpr void Hold(std::int64_t now) { held_ = true; deadline_ = now + kTimeoutUs; }
    constexpr void Heartbeat(std::int64_t now) { if (held_) deadline_ = now + kTimeoutUs; }
    constexpr void Release() { held_ = false; deadline_ = 0; }
    constexpr bool Expire(std::int64_t now) {
        if (!held_ || now < deadline_) return false;
        Release(); return true;
    }
    constexpr bool held() const { return held_; }
private:
    std::int64_t deadline_ = 0;
    bool held_ = false;
};
