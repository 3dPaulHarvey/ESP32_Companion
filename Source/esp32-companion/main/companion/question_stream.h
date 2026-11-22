#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#if COMPANION_QUESTION_FIXTURE
#include <memory>
#endif

class AudioService;

namespace companion_question {

// One microphone producer appends to this bounded PSRAM allocation. Network
// consumers may read only the prefix published by available_samples. The
// allocation and its published prefix remain stable until all owners release it.
class QuestionStream {
public:
    static constexpr std::size_t kCapacitySamples = 160000;
    static constexpr std::size_t kPreRollSamples = 5120;

    struct Result {
        bool success = false;
        const char* reason = "pending";
        unsigned captured_samples = 0, published_samples = 0, elapsed_ms = 0;
        unsigned frames = 0, speech_frames = 0, longest = 0, endpoint = 0;
        unsigned rms = 0, peak = 0, clipped = 0;
    };

    explicit QuestionStream(std::uint32_t request);
    ~QuestionStream();
    QuestionStream(const QuestionStream&) = delete;
    QuestionStream& operator=(const QuestionStream&) = delete;

    bool ready() const { return pcm_ != nullptr; }
    std::uint32_t request() const { return request_; }
    const std::int16_t* data() const { return pcm_; }
#if COMPANION_QUESTION_FIXTURE
    // Copy only explicitly supplied public synthetic PCM. Publication belongs
    // to the fixture task; the microphone is never opened for this source.
    static std::shared_ptr<QuestionStream> FromFixture(std::uint32_t request, const std::int16_t* pcm, std::size_t samples);
#endif

    std::atomic<std::size_t> available_samples{0};
    std::atomic<bool> finished{false}, cancelled{false}, speech_started{false};
    std::atomic<std::int64_t> recording_started_us{0}, speech_end_us{0};
    // Read only after acquiring finished=true; no audio or transcript is logged.
    Result result{};

private:
    friend class ::AudioService;
    std::uint32_t request_ = 0;
    std::int16_t* pcm_ = nullptr;
    std::int16_t* PreRoll() { return pcm_ + kCapacitySamples; }
};

}  // namespace companion_question
