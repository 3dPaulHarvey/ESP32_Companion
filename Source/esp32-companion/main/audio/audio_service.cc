#include "audio_service.h"
#include "stream_buffer_policy.h"
#include <esp_log.h>
#include <cstring>
#include <algorithm>
#include <new>
#include <esp_timer.h>
#include "wake_words/afe_wake_word.h"
#include "companion/numeric_log.h"

#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
#include "local_capture_config.h"
#include <cmath>
#include <cstdint>
#include <esp_heap_caps.h>
#endif
#if CONFIG_COMPANION_FOLLOWUP
#include "companion/speech_endpoint_policy.h"
#include <esp_vad.h>
#endif

#define RATE_CVT_CFG(_src_rate, _dest_rate, _channel)        \
    (esp_ae_rate_cvt_cfg_t)                                  \
    {                                                        \
        .src_rate        = (uint32_t)(_src_rate),            \
        .dest_rate       = (uint32_t)(_dest_rate),           \
        .channel         = (uint8_t)(_channel),              \
        .bits_per_sample = ESP_AUDIO_BIT16,                  \
        .complexity      = 2,                                \
        .perf_type       = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,  \
    }

#define OPUS_DEC_CFG(_sample_rate)                 \
    (esp_opus_dec_cfg_t)                            \
    {                                               \
        .sample_rate = (uint32_t)(_sample_rate),    \
        .channel = ESP_AUDIO_MONO,                  \
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS, \
        .self_delimited = false,                   \
    }

#define TAG "AudioService"

AudioService::AudioService() {
    event_group_ = xEventGroupCreate();
    ESP_ERROR_CHECK(event_group_ ? ESP_OK : ESP_ERR_NO_MEM);
}

AudioService::~AudioService() {
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
    if (opus_decoder_ != nullptr) {
        esp_opus_dec_close(opus_decoder_);
    }
    if (output_resampler_ != nullptr) {
        esp_ae_rate_cvt_close(output_resampler_);
    }
}

void AudioService::Initialize(AudioCodec* codec) {
    codec_ = codec;
    codec_->Start();

    SetDecodeSampleRate(codec_->output_sample_rate());
}

void AudioService::Start() {
    xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
    xEventGroupClearBits(event_group_, AS_EVENT_LOCAL_CAPTURE_RUNNING);
#endif
#if CONFIG_COMPANION_FOLLOWUP
    xEventGroupClearBits(event_group_, AS_EVENT_QUESTION_STREAM_RUNNING);
#endif
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore([](void* arg) {
        static_cast<AudioService*>(arg)->AudioInputTask();
        vTaskDelete(nullptr);
    }, "audio_input", 2048 * 3, this, 8, nullptr, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate([](void* arg) {
        static_cast<AudioService*>(arg)->AudioOutputTask();
        vTaskDelete(nullptr);
    }, "audio_output", 2048 * 2, this, 4, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate([](void* arg) {
        static_cast<AudioService*>(arg)->CueDecoderTask();
        vTaskDelete(nullptr);
    }, "cue_decoder", 2048 * 12, this, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

bool AudioService::ReadAudioData(std::vector<int16_t>& data, int samples) {
    data.resize(samples);
    return codec_->InputData(data);
}

void AudioService::AudioInputTask() {
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
    int64_t level_window_start_us = 0;
    uint64_t level_square_sum = 0;
    uint32_t level_samples = 0;
    uint32_t level_clipped = 0;
    int32_t level_peak = 0;
    auto report_mic_levels = [&](const std::vector<int16_t>& data) {
        if (data.empty()) {
            return;
        }
        const int64_t now_us = esp_timer_get_time();
        if (level_window_start_us == 0) {
            level_window_start_us = now_us;
        }
        // The installed I2S microphone is mono.
        for (size_t i = 0; i < data.size(); ++i) {
            const int32_t sample = data[i];
            const int32_t magnitude = sample < 0 ? -sample : sample;
            level_square_sum += static_cast<uint64_t>(magnitude) * magnitude;
            ++level_samples;
            if (magnitude > level_peak) {
                level_peak = magnitude;
            }
            // The existing codec clips negative overflow to -32767.
            if (magnitude >= 32767) {
                ++level_clipped;
            }
        }
        if (now_us - level_window_start_us >= 1000000) {
            const uint32_t rms = static_cast<uint32_t>(
                std::sqrt(static_cast<double>(level_square_sum) / level_samples));
            ESP_LOGI("MicDiagnostics", "Mic level: samples=%lu rms=%lu peak=%ld clipped=%lu",
                static_cast<unsigned long>(level_samples), static_cast<unsigned long>(rms),
                static_cast<long>(level_peak), static_cast<unsigned long>(level_clipped));
            level_window_start_us = 0;
            level_square_sum = 0;
            level_samples = 0;
            level_clipped = 0;
            level_peak = 0;
        }
    };
#endif

    while (true) {
        EventBits_t input_mask = AS_EVENT_WAKE_WORD_RUNNING;
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
        input_mask |= AS_EVENT_LOCAL_CAPTURE_RUNNING;
#endif
#if CONFIG_COMPANION_FOLLOWUP
        input_mask |= AS_EVENT_QUESTION_STREAM_RUNNING;
#endif
        EventBits_t bits = xEventGroupWaitBits(event_group_, input_mask,
            pdFALSE, pdFALSE, portMAX_DELAY);

#if CONFIG_COMPANION_FOLLOWUP
        if (bits & AS_EVENT_QUESTION_STREAM_RUNNING) {
            RunQuestionStream();
            continue;
        }
#endif
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
        // This task is the sole microphone reader for wake and question PCM.
        if (bits & AS_EVENT_LOCAL_CAPTURE_RUNNING) {
            RunLocalCapture();
            continue;
        }
#endif
        /* Feed the wake word */
        if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
            std::vector<int16_t> data;
            int samples = wake_word_->GetFeedSize();
            if (samples > 0) {
                if (ReadAudioData(data, samples)) {
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
                    report_mic_levels(data);
#endif
                    wake_word_->Feed(data);
                    continue;
                }
            }
        }

        ESP_LOGE(TAG, "Should not be here, bits: %lx", bits);
        break;
    }

    ESP_LOGW(TAG, "Audio input task stopped");
}

#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
bool AudioService::BeginLocalCapture(uint32_t request_id, bool retain_question) {
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
#if CONFIG_COMPANION_FOLLOWUP
    if (question_stream_) return false;
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    if (question_audio_) return false;
#else
    if (retain_question) return false;
#endif
    if (codec_ == nullptr || local_capture_requested_ || local_capture_result_ready_) {
        return false;
    }
    if (codec_->input_sample_rate() != LocalPcmCaptureBuffer::kSampleRate || codec_->input_channels() != 1) {
        return false;
    }
    const EventBits_t bits = xEventGroupGetBits(event_group_);
    if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
        return false;
    }
    local_capture_request_id_ = request_id;
#if CONFIG_COMPANION_STANDALONE
    local_capture_recording_id_.store(0);
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    local_capture_retain_question_ = retain_question;
#endif
    ++local_capture_generation_;
    local_capture_cancel_requested_ = false;
    local_capture_requested_ = true;
    xEventGroupSetBits(event_group_, AS_EVENT_LOCAL_CAPTURE_RUNNING);
    return true;
}

bool AudioService::PollLocalCaptureResult(LocalPcmCaptureResult& result) {
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (!local_capture_result_ready_) {
        return false;
    }
    result = local_capture_result_;
    local_capture_result_ready_ = false;
    return true;
}

void AudioService::CancelLocalCapture(uint32_t request_id) {
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (local_capture_requested_ && local_capture_request_id_ == request_id) {
        local_capture_cancel_requested_ = true;
    }
}

#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
std::unique_ptr<companion_question::Pcm> AudioService::TakeQuestionAudio(uint32_t request_id) {
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (!question_audio_ || question_audio_->request() != request_id) return nullptr;
    return std::move(question_audio_);
}
void AudioService::DiscardQuestionAudio(uint32_t request_id) {
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (question_audio_ && question_audio_->request() == request_id) question_audio_.reset();
}
#endif

void AudioService::RunLocalCapture() {
    uint32_t request_id;
    uint32_t generation;
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
    static_assert(companion_question::kSamples == companion_capture::kSamples);
    bool retain_question;
    std::unique_ptr<companion_question::Pcm> retained;
#endif
    {
        std::lock_guard<std::mutex> lock(local_capture_mutex_);
        if (!local_capture_requested_) {
            xEventGroupClearBits(event_group_, AS_EVENT_LOCAL_CAPTURE_RUNNING);
            return;
        }
        request_id = local_capture_request_id_;
        generation = local_capture_generation_;
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
        retain_question = local_capture_retain_question_;
#endif
    }

    const int64_t started_us = esp_timer_get_time();
    LocalPcmCaptureResult result;
    result.request_id = request_id;
    {
        // Local/calibration segments are erased before metadata publication.
        // Only an explicit question can move this allocation to its session.
        struct CaptureStorage {
            int16_t* data = static_cast<int16_t*>(heap_caps_malloc(
                companion_capture::kBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            ~CaptureStorage() {
                if (data != nullptr) {
                    volatile int16_t* wipe = data;
                    for (size_t i = 0; i < companion_capture::kSamples; ++i) {
                        wipe[i] = 0;
                    }
                    heap_caps_free(data);
                }
            }
        } storage;
        LocalPcmCaptureBuffer capture(storage.data, companion_capture::kSamples, companion_capture::kSamples);
#if CONFIG_COMPANION_MICROPHONE_PREP
        struct RawChunk {
            std::unique_ptr<int32_t[]> data{new (std::nothrow) int32_t[320]};
            ~RawChunk() { if (data) { volatile int32_t* wipe = data.get(); for (unsigned i = 0; i < 320; ++i) wipe[i] = 0; } }
        } raw;
        microphone_pcm::Headroom headroom(companion_capture::kPcmShift);
        bool recording_reported = false;
#endif
        LocalPcmCaptureStatus status = LocalPcmCaptureStatus::Success;
        if (!capture.valid()) {
            status = LocalPcmCaptureStatus::AllocationFailed;
#if CONFIG_COMPANION_MICROPHONE_PREP
        } else if (!raw.data) {
            status = LocalPcmCaptureStatus::AllocationFailed;
#endif
        } else if (codec_ == nullptr || codec_->input_sample_rate() != LocalPcmCaptureBuffer::kSampleRate ||
                   codec_->input_channels() != 1) {
            status = LocalPcmCaptureStatus::UnsupportedFormat;
        } else {
            int16_t chunk[320];
            while (!capture.complete()) {
                {
                    std::lock_guard<std::mutex> lock(local_capture_mutex_);
                    if (local_capture_generation_ != generation || local_capture_cancel_requested_) {
                        status = LocalPcmCaptureStatus::Cancelled;
                    }
                }
                if (status != LocalPcmCaptureStatus::Success) {
                    break;
                }
                if (esp_timer_get_time() - started_us >= companion_capture::kWorkerTimeoutMs * 1000LL) {
                    status = LocalPcmCaptureStatus::Timeout;
                    break;
                }
                const size_t requested = std::min<size_t>(320, capture.remaining_input_samples());
                size_t received = 0;
#if CONFIG_COMPANION_MICROPHONE_PREP
                const esp_err_t read_result = codec_->ReadLocalCaptureRaw(raw.data.get(), requested, received, 100);
#else
                const esp_err_t read_result = codec_->ReadLocalCaptureInput(chunk, requested, received, 100);
#endif
                if (read_result == ESP_ERR_NOT_SUPPORTED) {
                    status = LocalPcmCaptureStatus::UnsupportedFormat;
                    break;
                }
                if (read_result == ESP_ERR_NO_MEM) {
                    status = LocalPcmCaptureStatus::AllocationFailed;
                    break;
                }
                if ((read_result != ESP_OK && read_result != ESP_ERR_TIMEOUT) || received > requested) {
                    status = LocalPcmCaptureStatus::ReadError;
                    break;
                }
                if (received > 0) {
#if CONFIG_COMPANION_MICROPHONE_PREP
                    const auto discard = std::min(received, capture.discard_remaining());
                    if (!recording_reported && discard < received) {
                        recording_reported = true;
#if CONFIG_COMPANION_STANDALONE
                        local_capture_recording_id_.store(request_id);
#endif
                        ESP_LOGI("MicrophonePrep", "RECORDING request=%lu duration_ms=%u",
                            static_cast<unsigned long>(request_id), companion_capture::kDurationMs);
                    }
                    for (size_t i = 0; i < received; ++i) {
                        chunk[i] = microphone_pcm::Convert(raw.data[i], companion_capture::kPcmShift);
                        if (i >= discard) headroom.Feed(raw.data[i]);
                    }
#endif
                    capture.Feed(chunk, received);
                } else {
                    // A finite read with no data is retried until the service
                    // watchdog/cancel request; do not terminate the input task.
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
                if (esp_timer_get_time() - started_us >= companion_capture::kWorkerTimeoutMs * 1000LL) {
                    status = LocalPcmCaptureStatus::Timeout;
                    break;
                }
            }
        }
        result = capture.Result(request_id, status, static_cast<uint32_t>((esp_timer_get_time() - started_us) / 1000));
#if CONFIG_COMPANION_MICROPHONE_PREP
        result.microphone = headroom.Result();
#endif
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
        if (result.success && retain_question) {
            retained.reset(new (std::nothrow) companion_question::Pcm(request_id, storage.data));
            if (retained) storage.data = nullptr;
            else { result.success = false; result.status = LocalPcmCaptureStatus::AllocationFailed; }
        }
#endif
    }

    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (local_capture_requested_ && local_capture_generation_ == generation) {
        // A cancellation arriving after the final read but before publication
        // still receives a failure acknowledgment, with honest sample count.
        if (local_capture_cancel_requested_) {
            result.success = false;
            result.status = LocalPcmCaptureStatus::Cancelled;
        }
#if CONFIG_COMPANION_GEMINI_QUESTION_PROBE
        if (!result.success) retained.reset();
        question_audio_ = std::move(retained);
#endif
        xEventGroupClearBits(event_group_, AS_EVENT_LOCAL_CAPTURE_RUNNING);
        local_capture_result_ = result;
#if CONFIG_COMPANION_STANDALONE
        local_capture_recording_id_.store(0);
#endif
        local_capture_result_ready_ = true;
        local_capture_requested_ = false;
        local_capture_cancel_requested_ = false;
    }
}
#endif

#if CONFIG_COMPANION_FOLLOWUP
bool AudioService::BeginQuestionStream(uint32_t request_id) {
    if (!request_id || IsDiagnosticPcmBusy()) return false;
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (codec_ == nullptr || local_capture_requested_ || local_capture_result_ready_ || question_audio_ || question_stream_ ||
        codec_->input_sample_rate() != 16000 || codec_->input_channels() != 1 ||
        (xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING)) return false;
    try {
        question_stream_ = std::make_shared<companion_question::QuestionStream>(request_id);
    } catch (const std::bad_alloc&) {
        return false;
    }
    if (!question_stream_->ready()) { question_stream_.reset(); return false; }
#if CONFIG_COMPANION_STANDALONE
    local_capture_recording_id_.store(0);
#endif
    xEventGroupSetBits(event_group_, AS_EVENT_QUESTION_STREAM_RUNNING);
    return true;
}

#if COMPANION_QUESTION_FIXTURE
bool AudioService::BeginQuestionStreamFixture(uint32_t request_id, const int16_t* pcm, size_t samples, bool speech) {
    if (!request_id || IsDiagnosticPcmBusy()) return false;
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (local_capture_requested_ || local_capture_result_ready_ || question_audio_ || question_stream_ ||
        (xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING)) return false;
    auto stream = companion_question::QuestionStream::FromFixture(request_id, pcm, samples);
    if (!stream) return false;
    struct Publication {
        AudioService* audio;
        std::shared_ptr<companion_question::QuestionStream> stream;
        size_t samples;
        bool speech;
    };
    auto* publication = new (std::nothrow) Publication{this, stream, samples, speech};
    if (!publication) return false;
    question_stream_ = stream;
    const auto created = xTaskCreate([](void* argument) {
        std::unique_ptr<Publication> context(static_cast<Publication*>(argument));
        auto& state = *context->stream;
        const auto started = esp_timer_get_time();
        const auto deadline = started + static_cast<std::int64_t>(context->samples) * 1000000 / 16000 + 1000000;
        state.recording_started_us.store(started, std::memory_order_release);
#if CONFIG_COMPANION_STANDALONE
        context->audio->local_capture_recording_id_.store(state.request());
#endif
        size_t produced = 0;
        while (produced < context->samples && !state.cancelled.load(std::memory_order_acquire) && esp_timer_get_time() < deadline) {
            const auto count = std::min<size_t>(320, context->samples - produced);
            const auto target = started + static_cast<std::int64_t>(produced + count) * 1000000 / 16000;
            while (esp_timer_get_time() < target && !state.cancelled.load(std::memory_order_acquire)) vTaskDelay(1);
            if (state.cancelled.load(std::memory_order_acquire) || esp_timer_get_time() >= deadline) break;
            produced += count;
            if (context->speech) {
                state.available_samples.store(produced, std::memory_order_release);
                state.speech_started.store(true, std::memory_order_release);
            }
        }
        auto& result = state.result;
        result.captured_samples = produced;
        result.published_samples = context->speech ? produced : 0;
        result.elapsed_ms = static_cast<unsigned>((esp_timer_get_time() - started) / 1000);
        result.success = produced == context->samples;
        result.reason = result.success ? (context->speech ? "fixture_speech" : "quiet_timeout") : "fixture_timeout";
        // These fixtures bypass microphone/VAD deliberately; do not invent
        // speech-frame or endpoint metrics for the synthetic publication.
        std::uint64_t squares = 0;
        for (size_t i = 0; i < produced; ++i) {
            const int sample = state.data()[i];
            const unsigned magnitude = sample < 0 ? -sample : sample;
            result.peak = std::max(result.peak, magnitude);
            if (sample == -32768 || sample == 32767) ++result.clipped;
            squares += static_cast<std::uint64_t>(static_cast<std::int64_t>(sample) * sample);
        }
        result.rms = produced ? static_cast<unsigned>(std::sqrt(static_cast<double>(squares) / produced)) : 0;
        if (context->speech && result.success) state.speech_end_us.store(esp_timer_get_time(), std::memory_order_release);
        {
            std::lock_guard<std::mutex> capture_lock(context->audio->local_capture_mutex_);
            if (state.cancelled.load(std::memory_order_acquire)) { result.success = false; result.reason = "cancelled"; }
#if CONFIG_COMPANION_STANDALONE
            context->audio->local_capture_recording_id_.store(0);
#endif
            ESP_LOGI("QuestionStream", "FIXTURE request=%lu success=%d reason=%s samples=%u published_samples=%u elapsed_ms=%u microphone=0",
                static_cast<unsigned long>(state.request()), result.success, result.reason, result.captured_samples,
                result.published_samples, result.elapsed_ms);
            state.finished.store(true, std::memory_order_release);
        }
        // Destroy the shared ownership before deleting the FreeRTOS task;
        // vTaskDelete does not unwind this C++ stack.
        context.reset();
        vTaskDelete(nullptr);
    }, "question_fixture_stream", 3072, publication, 3, nullptr);
    if (created != pdPASS) {
        question_stream_.reset();
        delete publication;
        return false;
    }
    return true;
}
#endif

std::shared_ptr<companion_question::QuestionStream> AudioService::GetQuestionStream(uint32_t request_id) {
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    return question_stream_ && question_stream_->request() == request_id ? question_stream_ : nullptr;
}

void AudioService::CancelQuestionStream(uint32_t request_id) {
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (question_stream_ && question_stream_->request() == request_id)
        question_stream_->cancelled.store(true, std::memory_order_release);
}

bool AudioService::ReleaseQuestionStream(uint32_t request_id) {
    std::lock_guard<std::mutex> lock(local_capture_mutex_);
    if (!question_stream_ || question_stream_->request() != request_id) return true;
    if (!question_stream_->finished.load(std::memory_order_acquire)) {
        question_stream_->cancelled.store(true, std::memory_order_release);
        return false;
    }
    question_stream_.reset();
    return true;
}

void AudioService::RunQuestionStream() {
    static_assert(companion_question::QuestionStream::kCapacitySamples == companion_endpoint::kMaximumSamples);
    static_assert(companion_question::QuestionStream::kPreRollSamples == companion_endpoint::kPreRollFrames * companion_endpoint::kFrameSamples);
    std::shared_ptr<companion_question::QuestionStream> stream;
    {
        std::lock_guard<std::mutex> lock(local_capture_mutex_);
        stream = question_stream_;
    }
    if (!stream) { xEventGroupClearBits(event_group_, AS_EVENT_QUESTION_STREAM_RUNNING); return; }
    using companion_endpoint::Decision;
    companion_endpoint::Policy endpoint;
    const auto started = esp_timer_get_time();
    const auto deadline = started + 12500000;
    std::int64_t last_speech_us = 0;
    struct Vad {
        vad_handle_t handle = vad_create(VAD_MODE_3);
        ~Vad() { if (handle) vad_destroy(handle); }
    } vad;
    struct Raw {
        std::unique_ptr<int32_t[]> data{new (std::nothrow) int32_t[320]};
        ~Raw() { if (data) { volatile int32_t* wipe = data.get(); for (unsigned i = 0; i < 320; ++i) wipe[i] = 0; } }
    } raw;
    int16_t frame[320]{};
    std::size_t frame_used = 0, published = 0;
    companion_audio::QuestionPublisher publisher(stream->pcm_, companion_question::QuestionStream::kCapacitySamples,
        stream->PreRoll(), companion_question::QuestionStream::kPreRollSamples);
    unsigned warmup = LocalPcmCaptureBuffer::kDiscardSamples;
    std::uint64_t squares = 0;
    auto& result = stream->result;
    result.reason = "capturing";
    if (!vad.handle) result.reason = "vad_allocation_failed";
    else if (!raw.data) result.reason = "allocation_failed";
    else if (!codec_ || codec_->input_sample_rate() != 16000 || codec_->input_channels() != 1) result.reason = "unsupported_format";
    else {
        while (!endpoint.finished()) {
            if (stream->cancelled.load(std::memory_order_acquire)) { result.reason = "cancelled"; break; }
            if (esp_timer_get_time() >= deadline) { result.reason = "capture_timeout"; break; }
            const std::size_t requested = warmup ? std::min<unsigned>(320, warmup) : 320 - frame_used;
            std::size_t received = 0;
            const auto error = codec_->ReadLocalCaptureRaw(raw.data.get(), requested, received, 100);
            if ((error != ESP_OK && error != ESP_ERR_TIMEOUT) || received > requested) { result.reason = "read_error"; break; }
            if (!received) { vTaskDelay(1); continue; }
            if (warmup) { warmup -= received; continue; }
            if (!stream->recording_started_us.load(std::memory_order_relaxed)) {
                stream->recording_started_us.store(esp_timer_get_time(), std::memory_order_release);
#if CONFIG_COMPANION_STANDALONE
                local_capture_recording_id_.store(stream->request());
#endif
                ESP_LOGI("QuestionStream", "RECORDING request=%lu max_ms=10000 quiet_ms=4000 silence_ms=600",
                    static_cast<unsigned long>(stream->request()));
            }
            for (std::size_t i = 0; i < received; ++i) frame[frame_used++] = microphone_pcm::Convert(raw.data[i], companion_capture::kPcmShift);
            if (frame_used != 320) continue;
            frame_used = 0;
            const bool previously_started = publisher.speech_started();
            const bool speech = vad_process(vad.handle, frame, 16000, 20) == VAD_SPEECH;
            if (speech) last_speech_us = esp_timer_get_time();
            const auto decision = endpoint.Feed(speech);
            result.captured_samples += 320;
            for (const int sample : frame) {
                const unsigned magnitude = sample < 0 ? -sample : sample;
                result.peak = std::max(result.peak, magnitude);
                if (sample == -32768 || sample == 32767) ++result.clipped;
                squares += static_cast<std::uint64_t>(static_cast<std::int64_t>(sample) * sample);
            }
            if (!publisher.Feed(frame, 320, endpoint.speech_started())) { result.reason = "capture_limit"; break; }
            published = publisher.published();
            if (published) {
                stream->available_samples.store(published, std::memory_order_release);
                if (!previously_started) {
                    stream->speech_started.store(true, std::memory_order_release);
                    ESP_LOGI("QuestionStream", "SPEECH request=%lu pre_roll_samples=%u mono_us=%s",
                        static_cast<unsigned long>(stream->request()), static_cast<unsigned>(published), companion_log::Decimal(esp_timer_get_time()).data());
                }
            }
            if (endpoint.finished()) {
                result.success = true;
                result.reason = decision == Decision::Quiet ? "quiet_timeout" : decision == Decision::Limit ? "utterance_limit" : "speech_end";
                if (endpoint.speech_started()) stream->speech_end_us.store(last_speech_us, std::memory_order_release);
            }
        }
    }
    result.published_samples = published;
    result.elapsed_ms = static_cast<unsigned>((esp_timer_get_time() - started) / 1000);
    result.frames = endpoint.frames(); result.speech_frames = endpoint.speech_frames(); result.longest = endpoint.longest();
    result.endpoint = static_cast<unsigned>(endpoint.decision());
    result.rms = result.captured_samples ? static_cast<unsigned>(std::sqrt(static_cast<double>(squares) / result.captured_samples)) : 0;
    publisher.Seal();
    std::fill(frame, frame + 320, 0);
    std::fill(stream->PreRoll(), stream->PreRoll() + companion_question::QuestionStream::kPreRollSamples, 0);
    {
        std::lock_guard<std::mutex> lock(local_capture_mutex_);
        // Cancellation through the service API is serialized with terminal
        // publication, including one arriving after the final I2S read.
        if (stream->cancelled.load(std::memory_order_acquire)) { result.success = false; result.reason = "cancelled"; }
        ESP_LOGI("QuestionStream", "CAPTURE request=%lu success=%d reason=%s captured_samples=%u published_samples=%u frames=%u speech_frames=%u longest=%u elapsed_ms=%u rms=%u peak=%u clipped=%u",
            static_cast<unsigned long>(stream->request()), result.success, result.reason, result.captured_samples, result.published_samples,
            result.frames, result.speech_frames, result.longest, result.elapsed_ms, result.rms, result.peak, result.clipped);
        xEventGroupClearBits(event_group_, AS_EVENT_QUESTION_STREAM_RUNNING);
#if CONFIG_COMPANION_STANDALONE
        local_capture_recording_id_.store(0);
#endif
        stream->finished.store(true, std::memory_order_release);
    }
}
#endif

void AudioService::AudioOutputTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() { return !audio_playback_queue_.empty(); });
        auto task = std::move(audio_playback_queue_.front());
        audio_playback_queue_.pop_front();
        output_in_progress_ = true;
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
        const bool diagnostic_can_write = !diagnostic_error_;
#endif
        audio_queue_cv_.notify_all();
        lock.unlock();
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
        if (task->diagnostic_pcm) {
            size_t written = 0;
            const auto error = diagnostic_can_write
                ? codec_->WriteDiagnosticPcm(task->pcm.data(), task->pcm.size(), written, 250) : ESP_FAIL;
            std::fill(task->pcm.begin(), task->pcm.end(), 0);
            lock.lock();
            if (!task->diagnostic_silence) diagnostic_written_ += written;
            if (error != ESP_OK || written != task->pcm.size()) diagnostic_error_ = true;
            lock.unlock();
        } else
#endif
        { codec_->OutputData(task->pcm); }
        lock.lock();
        output_in_progress_ = false;
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
        if (diagnostic_abandoned_ && audio_playback_queue_.empty()) {
            diagnostic_playback_ = false; diagnostic_abandoned_ = false;
        }
#endif
        audio_queue_cv_.notify_all();
    }
}

void AudioService::CueDecoderTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() {
            return (!audio_decode_queue_.empty() && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE);
        });

        /* Decode the audio from decode queue */
        if (!audio_decode_queue_.empty() && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE) {
            decode_in_progress_ = true;
            auto packet = std::move(audio_decode_queue_.front());
            audio_decode_queue_.pop_front();
            audio_queue_cv_.notify_all();
            lock.unlock();

            auto task = std::make_unique<AudioTask>();

            SetDecodeSampleRate(packet->sample_rate);
            if (opus_decoder_ != nullptr) {
                task->pcm.resize(decoder_frame_size_);
                esp_audio_dec_in_raw_t raw = {
                    .buffer = (uint8_t *)(packet->payload.data()),
                    .len = (uint32_t)(packet->payload.size()),
                    .consumed = 0,
                    .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
                };
                esp_audio_dec_out_frame_t out_frame = {
                    .buffer = (uint8_t *)(task->pcm.data()),
                    .len = (uint32_t)(task->pcm.size() * sizeof(int16_t)),
                    .decoded_size = 0,
                };
                esp_audio_dec_info_t dec_info = {};
                std::unique_lock<std::mutex> decoder_lock(decoder_mutex_);
                auto ret = esp_opus_dec_decode(opus_decoder_, &raw, &out_frame, &dec_info);
                decoder_lock.unlock();
                if (ret == ESP_AUDIO_ERR_OK) {
                    task->pcm.resize(out_frame.decoded_size / sizeof(int16_t));
                    if (decoder_sample_rate_ != codec_->output_sample_rate() && output_resampler_ != nullptr) {
                        uint32_t target_size = 0;
                        esp_ae_rate_cvt_get_max_out_sample_num(output_resampler_, task->pcm.size(), &target_size);
                        std::vector<int16_t> resampled(target_size);
                        uint32_t actual_output = target_size;
                        esp_ae_rate_cvt_process(output_resampler_, (esp_ae_sample_t)task->pcm.data(), task->pcm.size(),
                                                (esp_ae_sample_t)resampled.data(), &actual_output);
                        resampled.resize(actual_output);
                        task->pcm = std::move(resampled);
                    }
                    lock.lock();
                    audio_playback_queue_.push_back(std::move(task));
                    audio_queue_cv_.notify_all();
                } else {
                    ESP_LOGE(TAG, "Failed to decode audio after resize, error code: %d", ret);
                    lock.lock();
                }
            } else {
                ESP_LOGE(TAG, "Audio decoder is not configured");
                lock.lock();
            }
            decode_in_progress_ = false;
            audio_queue_cv_.notify_all();
        }
    }

}

void AudioService::SetDecodeSampleRate(int sample_rate) {
    if (decoder_sample_rate_ == sample_rate) {
        return;
    }
    std::unique_lock<std::mutex> decoder_lock(decoder_mutex_);
    if (opus_decoder_ != nullptr) {
        esp_opus_dec_close(opus_decoder_);
        opus_decoder_ = nullptr;
    }
    decoder_lock.unlock();
    esp_opus_dec_cfg_t opus_dec_cfg = OPUS_DEC_CFG(sample_rate);
    auto ret = esp_opus_dec_open(&opus_dec_cfg, sizeof(esp_opus_dec_cfg_t), &opus_decoder_);
    if (opus_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create audio decoder, error code: %d", ret);
        return;
    }
    decoder_sample_rate_ = sample_rate;
    decoder_frame_size_ = decoder_sample_rate_ / 1000 * 60;

    if (decoder_sample_rate_ != codec_->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", decoder_sample_rate_, codec_->output_sample_rate());
        if (output_resampler_ != nullptr) {
            esp_ae_rate_cvt_close(output_resampler_);
            output_resampler_ = nullptr;
        }
        esp_ae_rate_cvt_cfg_t output_resampler_cfg = RATE_CVT_CFG(
            decoder_sample_rate_, codec_->output_sample_rate(), ESP_AUDIO_MONO);
        auto resampler_ret = esp_ae_rate_cvt_open(&output_resampler_cfg, &output_resampler_);
        if (output_resampler_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create output resampler, error code: %d", resampler_ret);
        }
    }
}

bool AudioService::PushPacketToDecodeQueue(std::unique_ptr<AudioCuePacket> packet) {
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    if (diagnostic_playback_) return false;
#endif
    audio_queue_cv_.wait(lock, [this]() {
        return audio_decode_queue_.size() < MAX_DECODE_PACKETS_IN_QUEUE;
    });
    audio_decode_queue_.push_back(std::move(packet));
    audio_queue_cv_.notify_all();
    return true;
}

void AudioService::EnableWakeWordDetection(bool enable) {
    if (!wake_word_) {
        return;
    }

    ESP_LOGD(TAG, "%s wake word detection", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!wake_word_initialized_) {
            if (!wake_word_->Initialize(models_list_)) {
                ESP_LOGE(TAG, "Failed to initialize wake word");
                return;
            }
            wake_word_initialized_ = true;
        }
        wake_word_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    } else {
        wake_word_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    }
}

void AudioService::SetCallbacks(AudioServiceCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void AudioService::PlaySound(const std::string_view& ogg) {
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(ogg.data());
    size_t size = ogg.size();
    size_t offset = 0;

    auto find_page = [&](size_t start)->size_t {
        for (size_t i = start; i + 4 <= size; ++i) {
            if (buf[i] == 'O' && buf[i+1] == 'g' && buf[i+2] == 'g' && buf[i+3] == 'S') return i;
        }
        return static_cast<size_t>(-1);
    };

    bool seen_head = false;
    bool seen_tags = false;
    int sample_rate = 16000; // 默认值

    while (true) {
        size_t pos = find_page(offset);
        if (pos == static_cast<size_t>(-1)) break;
        offset = pos;
        if (offset + 27 > size) break;

        const uint8_t* page = buf + offset;
        uint8_t page_segments = page[26];
        size_t seg_table_off = offset + 27;
        if (seg_table_off + page_segments > size) break;

        size_t body_size = 0;
        for (size_t i = 0; i < page_segments; ++i) body_size += page[27 + i];

        size_t body_off = seg_table_off + page_segments;
        if (body_off + body_size > size) break;

        // Parse packets using lacing
        size_t cur = body_off;
        size_t seg_idx = 0;
        while (seg_idx < page_segments) {
            size_t pkt_len = 0;
            size_t pkt_start = cur;
            bool continued = false;
            do {
                uint8_t l = page[27 + seg_idx++];
                pkt_len += l;
                cur += l;
                continued = (l == 255);
            } while (continued && seg_idx < page_segments);

            if (pkt_len == 0) continue;
            const uint8_t* pkt_ptr = buf + pkt_start;

            if (!seen_head) {
                // 解析OpusHead包
                if (pkt_len >= 19 && std::memcmp(pkt_ptr, "OpusHead", 8) == 0) {
                    seen_head = true;
                    // OpusHead结构：[0-7] "OpusHead", [8] version, [9] channel_count, [10-11] pre_skip
                    // [12-15] input_sample_rate, [16-17] output_gain, [18] mapping_family
                    if (pkt_len >= 12) {
                        uint8_t version = pkt_ptr[8];
                        uint8_t channel_count = pkt_ptr[9];
                        if (pkt_len >= 16) {
                            // 读取输入采样率 (little-endian)
                            sample_rate = pkt_ptr[12] | (pkt_ptr[13] << 8) |
                                        (pkt_ptr[14] << 16) | (pkt_ptr[15] << 24);
                            ESP_LOGI(TAG, "OpusHead: version=%d, channels=%d, sample_rate=%d",
                                   version, channel_count, sample_rate);
                        }
                    }
                }
                continue;
            }
            if (!seen_tags) {
                // Expect OpusTags in second packet
                if (pkt_len >= 8 && std::memcmp(pkt_ptr, "OpusTags", 8) == 0) {
                    seen_tags = true;
                }
                continue;
            }

            // Audio packet (Opus)
            auto packet = std::make_unique<AudioCuePacket>();
            packet->sample_rate = sample_rate;
            packet->payload.resize(pkt_len);
            std::memcpy(packet->payload.data(), pkt_ptr, pkt_len);
            PushPacketToDecodeQueue(std::move(packet));
        }

        offset = body_off + body_size;
    }
}

bool AudioService::IsIdle() {
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
    {
        std::lock_guard<std::mutex> capture_lock(local_capture_mutex_);
        if (local_capture_requested_) {
            return false;
        }
#if CONFIG_COMPANION_FOLLOWUP
        if (question_stream_ && !question_stream_->finished.load(std::memory_order_acquire)) return false;
#endif
    }
#endif
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    if (diagnostic_playback_ || output_in_progress_ || decode_in_progress_) return false;
#endif
    return !output_in_progress_ && !decode_in_progress_ && audio_decode_queue_.empty() && audio_playback_queue_.empty();
}

#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
bool AudioService::IsDiagnosticPcmBusy() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return diagnostic_playback_;
}

bool AudioService::PlayDiagnosticPcm(const int16_t* pcm, size_t samples, uint32_t& played) {
    played = 0;
    if (!pcm || !samples || samples > 24000 * 10 || codec_->output_sample_rate() != 24000 || codec_->output_channels() != 1) return false;
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    if (diagnostic_playback_ || output_in_progress_ || decode_in_progress_ ||
        !audio_decode_queue_.empty() || !audio_playback_queue_.empty()) return false;
    diagnostic_playback_ = true; diagnostic_error_ = false; diagnostic_written_ = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(samples / 24 + 2000);
    size_t offset = 0;
    // 80 ms of trailing silence exceeds this board's 60 ms DMA capacity.
    // Waiting for these writes lets the last real samples leave the speaker
    // before the UI returns to READY and wake detection is rearmed.
    while (offset < samples + 1920 && !diagnostic_error_) {
        if (!audio_queue_cv_.wait_until(lock, deadline, [this]() {
            return diagnostic_error_ || audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE;
        })) { diagnostic_error_ = true; break; }
        if (diagnostic_error_) break;
        const bool silence = offset >= samples;
        const size_t count = std::min<size_t>(480, (silence ? samples + 1920 : samples) - offset);
        auto task = std::make_unique<AudioTask>();
        task->diagnostic_pcm = true; task->diagnostic_silence = silence;
        if (silence) task->pcm.assign(count, 0);
        else task->pcm.assign(pcm + offset, pcm + offset + count);
        audio_playback_queue_.push_back(std::move(task));
        offset += count;
        audio_queue_cv_.notify_all();
    }
    if (!audio_queue_cv_.wait_until(lock, deadline, [this]() {
        return diagnostic_error_ || (audio_playback_queue_.empty() && !output_in_progress_);
    })) diagnostic_error_ = true;
    if (diagnostic_error_) {
        for (auto& task : audio_playback_queue_) std::fill(task->pcm.begin(), task->pcm.end(), 0);
        audio_playback_queue_.clear();
        audio_queue_cv_.notify_all();
        audio_queue_cv_.wait_for(lock, std::chrono::milliseconds(500), [this]() { return !output_in_progress_; });
    }
    played = diagnostic_written_;
    const bool success = !diagnostic_error_ && !output_in_progress_ && played == samples;
    diagnostic_abandoned_ = output_in_progress_;
    if (!diagnostic_abandoned_) diagnostic_playback_ = false;
    return success;
}

bool AudioService::PlayStreamingPcm(const int16_t* pcm, size_t capacity_samples, AudioPcmStream& stream, uint32_t& played) {
    played = 0;
    struct CompletionTime {
        AudioPcmStream& stream;
        ~CompletionTime() { stream.done_us.store(esp_timer_get_time(), std::memory_order_release); }
    } completion{stream};
    stream.done_us.store(0, std::memory_order_relaxed);
    if (!pcm || !capacity_samples || capacity_samples > 24000 * 10 || !codec_ ||
        codec_->output_sample_rate() != 24000 || codec_->output_channels() != 1) return false;
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    if (diagnostic_playback_ || output_in_progress_ || decode_in_progress_ ||
        !audio_decode_queue_.empty() || !audio_playback_queue_.empty()) return false;
    diagnostic_playback_ = true; diagnostic_error_ = false; diagnostic_written_ = 0;
    stream.started_us.store(0, std::memory_order_relaxed);
    stream.underruns.store(0, std::memory_order_relaxed);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto stopped = [&]() {
        return diagnostic_error_ || stream.cancelled.load(std::memory_order_acquire) ||
            std::chrono::steady_clock::now() >= deadline;
    };
    // Small responses may finish before the prebuffer is full. Longer ones
    // begin after 120 ms, while the network continues appending validated PCM.
    companion_audio::PlaybackPrefix prefix(capacity_samples);
    bool stalled = false;
    while (!stopped()) {
        const bool complete = stream.finished.load(std::memory_order_acquire);
        const auto available = stream.available_samples.load(std::memory_order_acquire);
        const auto slice = prefix.Next(available, complete);
        if (slice.action == companion_audio::PlaybackAction::Invalid) { diagnostic_error_ = true; break; }
        if (slice.action == companion_audio::PlaybackAction::Complete) break;
        if (slice.action == companion_audio::PlaybackAction::Wait) {
            if (!stalled && prefix.consumed() && audio_playback_queue_.empty() && !output_in_progress_) {
                stalled = true;
                stream.underruns.fetch_add(1, std::memory_order_relaxed);
            }
            audio_queue_cv_.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }
        if (audio_playback_queue_.size() >= MAX_PLAYBACK_TASKS_IN_QUEUE) {
            audio_queue_cv_.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }
        std::unique_ptr<AudioTask> task;
        try {
            task = std::make_unique<AudioTask>();
            task->pcm.assign(pcm + slice.offset, pcm + slice.offset + slice.samples);
        } catch (const std::bad_alloc&) {
            diagnostic_error_ = true; break;
        }
        task->diagnostic_pcm = true;
        if (!prefix.consumed()) stream.started_us.store(esp_timer_get_time(), std::memory_order_release);
        audio_playback_queue_.push_back(std::move(task));
        if (!prefix.Commit(slice.samples)) { diagnostic_error_ = true; break; }
        stalled = false;
        audio_queue_cv_.notify_all();
    }
    if (stopped() || !prefix.consumed()) diagnostic_error_ = true;
    // Flush the same 80 ms tail as the complete-buffer diagnostic path. This
    // happens once at EOF, never between incoming chunks or on cancellation.
    for (unsigned tail = 0; tail < 1920 && !stopped(); tail += 480) {
        while (!stopped() && audio_playback_queue_.size() >= MAX_PLAYBACK_TASKS_IN_QUEUE)
            audio_queue_cv_.wait_for(lock, std::chrono::milliseconds(10));
        if (stopped()) break;
        std::unique_ptr<AudioTask> task;
        try {
            task = std::make_unique<AudioTask>();
            task->pcm.assign(480, 0);
        } catch (const std::bad_alloc&) {
            diagnostic_error_ = true; break;
        }
        task->diagnostic_pcm = true; task->diagnostic_silence = true;
        audio_playback_queue_.push_back(std::move(task));
        audio_queue_cv_.notify_all();
    }
    while (!stopped() && (!audio_playback_queue_.empty() || output_in_progress_))
        audio_queue_cv_.wait_for(lock, std::chrono::milliseconds(10));
    if (stopped()) diagnostic_error_ = true;
    if (diagnostic_error_) {
        for (auto& task : audio_playback_queue_) std::fill(task->pcm.begin(), task->pcm.end(), 0);
        audio_playback_queue_.clear();
        audio_queue_cv_.notify_all();
        audio_queue_cv_.wait_for(lock, std::chrono::milliseconds(500), [this]() { return !output_in_progress_; });
    }
    played = diagnostic_written_;
    const bool success = !diagnostic_error_ && !output_in_progress_ && played == prefix.consumed();
    diagnostic_abandoned_ = output_in_progress_;
    if (!diagnostic_abandoned_) diagnostic_playback_ = false;
    return success;
}
#endif

void AudioService::SetModelsList(srmodel_list_t* models_list) {
    models_list_ = models_list;
    if (models_list_ && esp_srmodel_filter(models_list_, ESP_WN_PREFIX, nullptr)) {
        wake_word_ = std::make_unique<AfeWakeWord>();
    } else {
        wake_word_.reset();
    }

    if (wake_word_) {
        wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
            if (callbacks_.on_wake_word_detected) {
                callbacks_.on_wake_word_detected(wake_word);
            }
        });
    }
}
