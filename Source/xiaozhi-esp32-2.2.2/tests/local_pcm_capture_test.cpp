#include "../main/audio/local_pcm_capture.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using Buffer = LocalPcmCaptureBuffer;
using Status = LocalPcmCaptureStatus;

void TestDiscardExactBoundsAndOvershoot() {
    std::vector<int16_t> guarded(Buffer::kTargetSamples + 2, 777);
    Buffer capture(guarded.data() + 1, Buffer::kTargetSamples);
    std::vector<int16_t> discard(Buffer::kDiscardSamples, 32767);
    assert(capture.valid());
    assert(capture.Feed(discard.data(), discard.size()) == discard.size());
    assert(capture.Result(7, Status::Timeout, 200).samples == 0);
    std::vector<int16_t> input(Buffer::kTargetSamples + 1000, 1000);
    assert(capture.Feed(input.data(), input.size()) == Buffer::kTargetSamples);
    assert(capture.complete());
    assert(capture.remaining_input_samples() == 0);
    assert(capture.Feed(input.data(), input.size()) == 0);
    const auto result = capture.Result(7, Status::Success, 2200);
    assert(result.request_id == 7 && result.success && result.status == Status::Success);
    assert(result.samples == 32000 && result.sample_rate == 16000);
    assert(result.rms == 1000 && result.peak == 1000 && result.clipped == 0);
    assert(result.elapsed_ms == 2200);
    assert(guarded.front() == 777 && guarded.back() == 777);
}

void TestShortChunksAndStatistics() {
    std::vector<int16_t> pcm(Buffer::kTargetSamples);
    Buffer capture(pcm.data(), pcm.size());
    std::vector<int16_t> discard(Buffer::kDiscardSamples, -32768);
    capture.Feed(discard.data(), discard.size());
    const int16_t samples[] = {1000, -1000, 32767, -32768};
    assert(capture.Feed(samples, 4) == 4);
    const auto result = capture.Result(9, Status::Cancelled, 250);
    assert(!result.success && result.status == Status::Cancelled && result.samples == 4);
    assert(result.peak == 32768 && result.clipped == 2);
    const uint64_t square_sum = 1000000ULL + 1000000ULL + 32767ULL * 32767ULL + 32768ULL * 32768ULL;
    assert(result.rms == static_cast<uint32_t>(std::sqrt(static_cast<double>(square_sum) / 4)));
    assert(capture.Result(9, Status::Success, 250).status == Status::ReadError);
    assert(!capture.Result(9, Status::Success, 250).success);
}

void TestChunkedFullCaptureAndClear() {
    std::vector<int16_t> pcm(Buffer::kTargetSamples, 123);
    Buffer capture(pcm.data(), pcm.size());
    const int16_t value = -3000;
    for (size_t i = 0; i < Buffer::kDiscardSamples + Buffer::kTargetSamples; ++i) {
        assert(capture.Feed(&value, 1) == 1);
    }
    const auto complete = capture.Result(12, Status::Success, 2200);
    assert(complete.success && complete.samples == 32000 && complete.rms == 3000);
    assert(complete.peak == 3000 && complete.clipped == 0);
    capture.Clear();
    for (auto sample : pcm) { assert(sample == 0); }
    const auto cleared = capture.Result(13, Status::Timeout, 0);
    assert(cleared.samples == 0 && cleared.rms == 0 && cleared.peak == 0 && cleared.clipped == 0);
    assert(capture.remaining_input_samples() == Buffer::kTargetSamples + Buffer::kDiscardSamples);
    assert(!capture.complete());
}

void TestInvalidStorageAndEmptyInput() {
    std::vector<int16_t> small(10, 456);
    Buffer null_capture(nullptr, Buffer::kTargetSamples);
    Buffer short_capture(small.data(), small.size());
    const int16_t sample = 1234;
    assert(!null_capture.valid() && !short_capture.valid());
    assert(null_capture.Feed(&sample, 1) == 0);
    assert(short_capture.Feed(&sample, 1) == 0);
    for (auto value : small) { assert(value == 456); }
    assert(short_capture.Feed(nullptr, 1) == 0);
    const auto result = short_capture.Result(22, Status::AllocationFailed, 1);
    assert(!result.success && result.samples == 0 && result.rms == 0 && result.peak == 0);
    assert(std::strcmp(LocalPcmCaptureStatusName(Status::Timeout), "timeout") == 0);
}
}  // namespace

int main() {
    TestDiscardExactBoundsAndOvershoot();
    TestShortChunksAndStatistics();
    TestChunkedFullCaptureAndClear();
    TestInvalidStorageAndEmptyInput();
    std::cout << "Local PCM capture buffer tests passed.\n";
}
