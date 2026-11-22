#include "../main/audio/microphone_pcm.h"
#include "../main/audio/calibration_lease.h"
#include "../main/audio/local_pcm_capture.h"
#include "../main/local_companion_cycle.h"
#include <array>
#include <limits>

namespace {
using namespace microphone_pcm;
constexpr bool ExactBoundAndWarmup() {
    std::array<int16_t, 6> storage{77, 0, 0, 0, 0, 88};
    LocalPcmCaptureBuffer capture(storage.data() + 1, 4, 4, 2);
    constexpr int16_t first[] = {32767, -32768, 10};
    constexpr int16_t rest[] = {-20, 30, -40, 99};
    if (!capture.valid() || capture.remaining_input_samples() != 6 || capture.Feed(first, 3) != 3) return false;
    if (capture.discard_remaining() || capture.remaining_input_samples() != 3 || capture.complete()) return false;
    if (capture.Feed(rest, 4) != 3 || !capture.complete() || capture.Feed(rest, 1) != 0) return false;
    return storage[0] == 77 && storage[1] == 10 && storage[4] == -40 && storage[5] == 88;
}
constexpr bool InvalidCaptureLimits() {
    int16_t sample = 0;
    LocalPcmCaptureBuffer empty(&sample, 1, 0), oversized(&sample, 100000, 64001), short_buffer(&sample, 1, 2);
    return !empty.valid() && !oversized.valid() && !short_buffer.valid() && !empty.Feed(&sample, 1);
}
constexpr bool HeadroomBeforeClipping() {
    Headroom stats(14);
    if (!stats.Feed(65536 * 4096) || !stats.Feed(-65536 * 4096)) return false;
    return stats.samples() == 2 && stats.legacy_clipped() == 2 && stats.raw_peak() == 268435456 && !stats.raw_near_full_scale();
}
constexpr bool ExtremeRawIsCounted() {
    Headroom stats(16);
    return stats.Feed(std::numeric_limits<int32_t>::min()) && stats.Feed(std::numeric_limits<int32_t>::max()) &&
        stats.raw_peak() == 2147483648U && stats.raw_near_full_scale() == 2 && stats.legacy_clipped() == 2;
}
constexpr bool NewWatchdogAndRetry() {
    LocalCompanionCycle cycle(6000000);
    if (!cycle.BeginWake(0) || !cycle.Tick(100000) || cycle.Tick(4100000)) return false;
    if (!cycle.CaptureFinished(4300000) || !cycle.Tick(5100000) || cycle.completed() != 1) return false;
    cycle.Reset();
    if (!cycle.BeginWake(0) || !cycle.Tick(100000) || cycle.CaptureFinished(6100000) || !cycle.Tick(6100000)) return false;
    return cycle.aborted() == 1 && !cycle.completed() && cycle.BeginWake(7000000);
}
constexpr bool LeaseRecoversAfterDisconnect() {
    MicrophoneCalibrationLease lease;
    if (lease.held() || lease.Expire(0)) return false;
    lease.Hold(100);
    if (!lease.held() || lease.Expire(10000099)) return false;
    return lease.Expire(10000100) && !lease.held() && !lease.Expire(10000101);
}
constexpr bool HeartbeatAndRelease() {
    MicrophoneCalibrationLease lease;
    lease.Hold(0); lease.Heartbeat(5000000);
    if (lease.Expire(10000000) || !lease.held()) return false;
    lease.Release(); lease.Heartbeat(20000000);
    return !lease.held() && !lease.Expire(99999999);
}
static_assert(Convert(16384000, 14) == 1000);
static_assert(Convert(-16384000, 14) == -1000);
static_assert(Convert(std::numeric_limits<int32_t>::max(), 14) == 32767);
static_assert(Convert(std::numeric_limits<int32_t>::min(), 14) == -32768);
static_assert(Convert(std::numeric_limits<int32_t>::max(), 16) == 32767);
static_assert(Convert(std::numeric_limits<int32_t>::min(), 16) == -32768);
static_assert(Convert(100, 0) == 0 && Convert(100, 32) == 0);
static_assert(Magnitude(std::numeric_limits<int32_t>::min()) == 2147483648U);
static_assert(SuggestedShift(0) == 12);
static_assert(SuggestedShift(16384U << 12) == 12);
static_assert(SuggestedShift((16384U << 14) + 1) == 15);
static_assert(SuggestedShift(2147483648U) == 17);
static_assert(ExactBoundAndWarmup());
static_assert(InvalidCaptureLimits());
static_assert(HeadroomBeforeClipping());
static_assert(ExtremeRawIsCounted());
static_assert(NewWatchdogAndRetry());
static_assert(LeaseRecoversAfterDisconnect());
static_assert(HeartbeatAndRelease());
}  // namespace
