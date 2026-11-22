#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <memory>
#include <deque>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <atomic>
#include <functional>
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <model_path.h>
#include "esp_opus_dec.h"
#include "esp_ae_rate_cvt.h"
#include "esp_audio_types.h"

#include "audio_codec.h"
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
#include "local_pcm_capture.h"
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
#include "companion/question_audio.h"
#endif
#if CONFIG_COMPANION_FOLLOWUP
#include "companion/question_stream.h"
#endif

class AfeWakeWord;

// Microphone PCM feeds the local wake detector or the bounded question capture.
// Gemini replies are already PCM. Only the bundled OGG cue uses the decoder.
#define MAX_PLAYBACK_TASKS_IN_QUEUE 2
#define MAX_DECODE_PACKETS_IN_QUEUE 40
#define AS_EVENT_WAKE_WORD_RUNNING (1 << 1)
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
#define AS_EVENT_LOCAL_CAPTURE_RUNNING (1 << 4)
#endif
#if CONFIG_COMPANION_FOLLOWUP
#define AS_EVENT_QUESTION_STREAM_RUNNING (1 << 5)
#endif

#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
// The network owner publishes a validated, append-only PCM prefix. It must
// retain the allocation until PlayStreamingPcm returns, including on failure.
struct AudioPcmStream {
    std::atomic<std::size_t> available_samples{0};
    std::atomic<bool> finished{false}, cancelled{false};
    std::atomic<std::int64_t> started_us{0}, done_us{0};
    std::atomic<std::uint32_t> underruns{0};
};
#endif

struct AudioServiceCallbacks {
    std::function<void(const std::string&)> on_wake_word_detected;
};

struct AudioCuePacket {
    int sample_rate = 0;
    std::vector<uint8_t> payload;
};

struct AudioTask {
    std::vector<int16_t> pcm;
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    bool diagnostic_pcm = false;
    bool diagnostic_silence = false;
#endif
};

class AudioService {
public:
    static AudioService& GetInstance() {
        static AudioService instance;
        return instance;
    }

    AudioService();
    ~AudioService();
    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;

    void Initialize(AudioCodec* codec);
    void Start();
    bool IsIdle();
    bool IsWakeWordRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING; }
    bool IsAfeWakeWord() const { return wake_word_ != nullptr; }
    void EnableWakeWordDetection(bool enable);
    void SetCallbacks(AudioServiceCallbacks& callbacks);
    void PlaySound(const std::string_view& sound);
    void SetModelsList(srmodel_list_t* models_list);
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
    bool BeginLocalCapture(uint32_t request_id, bool retain_question = false);
    bool PollLocalCaptureResult(LocalPcmCaptureResult& result);
    void CancelLocalCapture(uint32_t request_id);
#if CONFIG_COMPANION_STANDALONE
    bool IsLocalCaptureRecording(uint32_t request_id) const { return request_id && local_capture_recording_id_.load() == request_id; }
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    std::unique_ptr<companion_question::Pcm> TakeQuestionAudio(uint32_t request_id);
    void DiscardQuestionAudio(uint32_t request_id);
#endif
#endif
#if CONFIG_COMPANION_FOLLOWUP
    bool BeginQuestionStream(uint32_t request_id);
#if COMPANION_QUESTION_FIXTURE
    bool BeginQuestionStreamFixture(uint32_t request_id, const int16_t* pcm, size_t samples, bool speech = true);
#endif
    std::shared_ptr<companion_question::QuestionStream> GetQuestionStream(uint32_t request_id);
    void CancelQuestionStream(uint32_t request_id);
    // Active capture is cancelled, but kept alive until its producer stops.
    bool ReleaseQuestionStream(uint32_t request_id);
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    bool PlayDiagnosticPcm(const int16_t* pcm, size_t samples, uint32_t& played);
    bool PlayStreamingPcm(const int16_t* pcm, size_t capacity_samples, AudioPcmStream& stream, uint32_t& played);
    bool IsDiagnosticPcmBusy();
#endif

private:
    AudioCodec* codec_ = nullptr;
    AudioServiceCallbacks callbacks_;
    std::unique_ptr<AfeWakeWord> wake_word_;
    void* opus_decoder_ = nullptr;
    std::mutex decoder_mutex_;
    esp_ae_rate_cvt_handle_t output_resampler_ = nullptr;
    int decoder_sample_rate_ = 0;
    int decoder_frame_size_ = 0;
    srmodel_list_t* models_list_ = nullptr;
    EventGroupHandle_t event_group_;

    std::mutex audio_queue_mutex_;
    std::condition_variable audio_queue_cv_;
    std::deque<std::unique_ptr<AudioCuePacket>> audio_decode_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_playback_queue_;
    bool output_in_progress_ = false;
    bool decode_in_progress_ = false;
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    bool diagnostic_playback_ = false;
    bool diagnostic_error_ = false;
    bool diagnostic_abandoned_ = false;
    uint32_t diagnostic_written_ = 0;
#endif
    bool wake_word_initialized_ = false;
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
    // The UI exchanges metadata; a question transfers its bounded PCM directly
    // from the input task to the session task.
    std::mutex local_capture_mutex_;
    uint32_t local_capture_request_id_ = 0;
    uint32_t local_capture_generation_ = 0;
    bool local_capture_requested_ = false;
    bool local_capture_cancel_requested_ = false;
    bool local_capture_result_ready_ = false;
#if CONFIG_COMPANION_STANDALONE
    std::atomic<uint32_t> local_capture_recording_id_{0};
#endif
    LocalPcmCaptureResult local_capture_result_;
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    bool local_capture_retain_question_ = false;
    std::unique_ptr<companion_question::Pcm> question_audio_;
#endif
#if CONFIG_COMPANION_FOLLOWUP
    std::shared_ptr<companion_question::QuestionStream> question_stream_;
    void RunQuestionStream();
#endif
    void RunLocalCapture();
#endif

    void AudioInputTask();
    void AudioOutputTask();
    void CueDecoderTask();
    bool ReadAudioData(std::vector<int16_t>& data, int samples);
    bool PushPacketToDecodeQueue(std::unique_ptr<AudioCuePacket> packet);
    void SetDecodeSampleRate(int sample_rate);
};

#endif
