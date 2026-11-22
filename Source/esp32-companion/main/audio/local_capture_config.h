#pragma once

#include "sdkconfig.h"
#include <cstddef>
#include <cstdint>

namespace companion_capture {
#if CONFIG_COMPANION_MICROPHONE_PREP
inline constexpr unsigned kDurationMs = 4000;
inline constexpr unsigned kPcmShift = CONFIG_COMPANION_MICROPHONE_SHIFT;
inline constexpr const char* kLabel = "local-4s";
#else
inline constexpr unsigned kDurationMs = 2000;
inline constexpr unsigned kPcmShift = 12;
inline constexpr const char* kLabel = "local-2s";
#endif
inline constexpr std::size_t kSamples = 16000 * kDurationMs / 1000;
inline constexpr std::size_t kBytes = kSamples * sizeof(std::int16_t);
inline constexpr unsigned kWorkerTimeoutMs = kDurationMs + 1500;
inline constexpr std::int64_t kControllerTimeoutUs = (kDurationMs + 2000) * 1000LL;
static_assert(kSamples <= 64000 && kPcmShift >= 12 && kPcmShift <= 18);
}  // namespace companion_capture
