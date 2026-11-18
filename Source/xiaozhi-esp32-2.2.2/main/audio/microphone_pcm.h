#pragma once

#include <array>
#include <cstdint>
#include <cmath>

namespace microphone_pcm {
constexpr bool ValidShift(unsigned shift) { return shift >= 12 && shift <= 18; }
constexpr std::int16_t Convert(std::int32_t raw, unsigned shift) {
    if (!ValidShift(shift)) return 0;
    const auto value = raw >> shift;
    return value > 32767 ? 32767 : value < -32768 ? -32768 : static_cast<std::int16_t>(value);
}
constexpr std::uint32_t Magnitude(std::int32_t value) {
    return static_cast<std::uint32_t>(value < 0 ? -static_cast<std::int64_t>(value) : value);
}
constexpr unsigned SuggestedShift(std::uint32_t raw_peak) {
    unsigned shift = 12;
    while (shift < 18 && ((static_cast<std::uint64_t>(raw_peak) + (1U << shift) - 1) >> shift) > 16384) ++shift;
    return shift;
}

struct Metrics {
    std::uint32_t samples = 0, raw_peak = 0, legacy_clipped = 0, raw_near_full_scale = 0;
    unsigned pcm_shift = 12, suggested_shift = 12;
    std::int32_t dc_mean = 0;
    std::uint32_t ac_rms = 0;
    std::array<std::uint32_t, 4> window_samples{}, window_rms{}, window_peak{}, window_clipped{};
};

// Fed only stored samples, after the warm-up discard. This compares the old
// gain and capture gain on the same raw data, before either can be clipped.
class Headroom {
public:
    explicit constexpr Headroom(unsigned shift) : shift_(shift) {}
    constexpr bool Feed(std::int32_t raw) {
        if (!ValidShift(shift_) || samples_ >= 64000) return false;
        const auto value = Convert(raw, shift_);
        const auto magnitude = Magnitude(value);
        const auto raw_magnitude = Magnitude(raw);
        if (raw_magnitude > raw_peak_) raw_peak_ = raw_magnitude;
        const auto legacy = raw >> 12;
        if (legacy >= 32767 || legacy <= -32767) ++legacy_clipped_;
        if (raw_magnitude >= 0x7fff0000U) ++raw_near_full_scale_;
        const auto window = samples_ / 16000;
        ++windows_[window].samples;
        windows_[window].squares += static_cast<std::uint64_t>(magnitude) * magnitude;
        if (magnitude > windows_[window].peak) windows_[window].peak = magnitude;
        if (magnitude >= 32767) ++windows_[window].clipped;
        squares_ += static_cast<std::uint64_t>(magnitude) * magnitude;
        sum_ += value; ++samples_;
        return true;
    }
    Metrics Result() const {
        Metrics result;
        result.samples = samples_; result.raw_peak = raw_peak_; result.legacy_clipped = legacy_clipped_;
        result.raw_near_full_scale = raw_near_full_scale_; result.pcm_shift = shift_;
        result.suggested_shift = SuggestedShift(raw_peak_);
        if (samples_) {
            const double mean = static_cast<double>(sum_) / samples_;
            const double variance = static_cast<double>(squares_) / samples_ - mean * mean;
            result.dc_mean = static_cast<std::int32_t>(sum_ / samples_);
            result.ac_rms = variance > 0 ? static_cast<std::uint32_t>(std::sqrt(variance)) : 0;
        }
        for (unsigned i = 0; i < windows_.size(); ++i) {
            result.window_samples[i] = windows_[i].samples;
            result.window_rms[i] = windows_[i].samples ? static_cast<std::uint32_t>(
                std::sqrt(static_cast<double>(windows_[i].squares) / windows_[i].samples)) : 0;
            result.window_peak[i] = windows_[i].peak; result.window_clipped[i] = windows_[i].clipped;
        }
        return result;
    }
    constexpr std::uint32_t samples() const { return samples_; }
    constexpr std::uint32_t raw_peak() const { return raw_peak_; }
    constexpr std::uint32_t legacy_clipped() const { return legacy_clipped_; }
    constexpr std::uint32_t raw_near_full_scale() const { return raw_near_full_scale_; }
private:
    struct Window { std::uint32_t samples = 0, peak = 0, clipped = 0; std::uint64_t squares = 0; };
    std::array<Window, 4> windows_{};
    unsigned shift_;
    std::uint32_t samples_ = 0, raw_peak_ = 0, legacy_clipped_ = 0, raw_near_full_scale_ = 0;
    std::uint64_t squares_ = 0;
    std::int64_t sum_ = 0;
};
}  // namespace microphone_pcm
