#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

enum class LocalPcmCaptureStatus {
    Success,
    Cancelled,
    Timeout,
    ReadError,
    UnsupportedFormat,
    AllocationFailed,
    ServiceStopped,
};

inline const char* LocalPcmCaptureStatusName(LocalPcmCaptureStatus status) {
    switch (status) {
        case LocalPcmCaptureStatus::Success: return "success";
        case LocalPcmCaptureStatus::Cancelled: return "cancelled";
        case LocalPcmCaptureStatus::Timeout: return "timeout";
        case LocalPcmCaptureStatus::ReadError: return "read_error";
        case LocalPcmCaptureStatus::UnsupportedFormat: return "unsupported_format";
        case LocalPcmCaptureStatus::AllocationFailed: return "allocation_failed";
        case LocalPcmCaptureStatus::ServiceStopped: return "service_stopped";
    }
    return "invalid";
}

struct LocalPcmCaptureResult {
    std::uint32_t request_id = 0;
    bool success = false;
    LocalPcmCaptureStatus status = LocalPcmCaptureStatus::ReadError;
    std::uint32_t samples = 0;
    std::uint32_t rms = 0;
    std::int32_t peak = 0;
    std::uint32_t clipped = 0;
    std::uint32_t elapsed_ms = 0;
    std::uint32_t sample_rate = 16000;
};

// Pure bounded accumulator over caller-owned storage. The audio input task is
// its only owner; neither PCM nor a storage pointer is exposed to the UI task.
class LocalPcmCaptureBuffer {
public:
    static constexpr std::size_t kSampleRate = 16000;
    static constexpr std::size_t kTargetSamples = 32000;
    static constexpr std::size_t kDiscardSamples = 3200;
    static constexpr std::size_t kBufferBytes = kTargetSamples * sizeof(std::int16_t);

    LocalPcmCaptureBuffer(std::int16_t* storage, std::size_t capacity)
        : storage_(storage), capacity_(capacity) {}

    bool valid() const { return storage_ != nullptr && capacity_ >= kTargetSamples; }
    bool complete() const { return samples_ == kTargetSamples; }
    std::size_t remaining_input_samples() const {
        return discard_remaining_ + kTargetSamples - samples_;
    }

    // Includes the initial discard in consumed input, never in stored samples
    // or statistics. Ignore overshoot after the fixed-size segment is full.
    std::size_t Feed(const std::int16_t* input, std::size_t count) {
        if (!valid() || input == nullptr) {
            return 0;
        }
        const std::size_t discarded = std::min(count, discard_remaining_);
        discard_remaining_ -= discarded;
        input += discarded;
        count -= discarded;
        const std::size_t stored = std::min(count, kTargetSamples - samples_);
        for (std::size_t i = 0; i < stored; ++i) {
            const std::int32_t value = input[i];
            const std::int32_t magnitude = value < 0 ? -value : value;
            storage_[samples_++] = input[i];
            square_sum_ += static_cast<std::uint64_t>(magnitude) * magnitude;
            peak_ = std::max(peak_, magnitude);
            if (magnitude >= 32767) {
                ++clipped_;
            }
        }
        return discarded + stored;
    }

    LocalPcmCaptureResult Result(std::uint32_t request_id, LocalPcmCaptureStatus status,
                                 std::uint32_t elapsed_ms) const {
        LocalPcmCaptureResult result;
        result.request_id = request_id;
        result.status = status == LocalPcmCaptureStatus::Success && !complete()
            ? LocalPcmCaptureStatus::ReadError : status;
        result.success = result.status == LocalPcmCaptureStatus::Success;
        result.samples = static_cast<std::uint32_t>(samples_);
        result.rms = samples_ == 0 ? 0 : static_cast<std::uint32_t>(
            std::sqrt(static_cast<double>(square_sum_) / samples_));
        result.peak = peak_;
        result.clipped = clipped_;
        result.elapsed_ms = elapsed_ms;
        return result;
    }

    void Clear() {
        if (storage_ != nullptr) {
            volatile std::int16_t* data = storage_;
            for (std::size_t i = 0; i < capacity_; ++i) {
                data[i] = 0;
            }
        }
        discard_remaining_ = kDiscardSamples;
        samples_ = 0;
        square_sum_ = 0;
        peak_ = 0;
        clipped_ = 0;
    }

private:
    std::int16_t* storage_;
    std::size_t capacity_;
    std::size_t discard_remaining_ = kDiscardSamples;
    std::size_t samples_ = 0;
    std::uint64_t square_sum_ = 0;
    std::int32_t peak_ = 0;
    std::uint32_t clipped_ = 0;
};

static_assert(LocalPcmCaptureBuffer::kBufferBytes == 64000, "Local PCM storage must remain bounded");
