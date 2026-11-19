#include "gemini_conversation.h"
#include "gemini_setup_policy.h"
#include "gemini_voice.h"
#include "question_audio.h"
#include "session_probe.h"
#if COMPANION_QUESTION_FIXTURE
#include "question_fixture.h"
#endif
#include "application.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_transport_ssl.h>
#include <esp_transport_ws.h>
#include <esp_vad.h>
#include <mbedtls/platform_util.h>
#include <wifi_manager.h>
#include <freertos/task.h>

#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_MBEDTLS_HAVE_TIME_DATE || CONFIG_ESP_TLS_INSECURE || !COMPANION_WS_LOGS_DISABLED
#error "Conversation transport requires verified TLS and compiled-out private transport logs."
#endif

namespace {
constexpr char kHost[] = "generativelanguage.googleapis.com";
constexpr char kTag[] = "CompanionConversation";
struct PrivateBuffer {
    char* data;
    std::size_t size;
    explicit PrivateBuffer(std::size_t capacity) : data(static_cast<char*>(heap_caps_calloc(1, capacity,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))), size(capacity) {}
    ~PrivateBuffer() { if (data) { mbedtls_platform_zeroize(data, size); heap_caps_free(data); } }
};
struct Playback {
    const int16_t* pcm;
    std::size_t samples;
    std::uint32_t played = 0;
    bool ok = false;
    std::atomic<bool> done{false};
};

class Conversation {
public:
    CompanionConversation::Result result{};
    bool Connect(std::string_view temporary) {
        if (!Alive()) return false;
        if (!path_.data || !message_.data || !upload_.data || !reply_.ready()) return Fail("allocation_failed");
        std::size_t length = 0;
        if (!companion_gemini::EncodePath(temporary, path_.data, path_.size, length)) return Fail("invalid_temporary_credential");
        tls_ = esp_transport_ssl_init();
        if (!tls_) return Fail("tls_init_failed");
        esp_transport_ssl_crt_bundle_attach(tls_, esp_crt_bundle_attach);
        esp_transport_ssl_set_common_name(tls_, kHost);
        ws_ = esp_transport_ws_init(tls_);
        if (!ws_) return Fail("websocket_init_failed");
        esp_transport_ws_config_t config{};
        config.ws_path = path_.data; config.user_agent = "ESP32-Companion-Conversation/0.1";
        config.propagate_control_frames = true;
        if (esp_transport_ws_set_config(ws_, &config) != ESP_OK) return Fail("websocket_config_failed");
        ESP_LOGI("CompanionGemini", "CONNECT_BEGIN no_question=0 no_audio_upload=0 conversation=1");
        const int connected = esp_transport_connect(ws_, kHost, 443, 8000);
        result.connection.upgrade_http = esp_transport_ws_get_upgrade_request_status(ws_);
        if (connected || result.connection.upgrade_http != 101) return Fail("websocket_upgrade_failed");
        char setup[] = "{\"setup\":{\"model\":\"models/gemini-3.8-live\"}}";
        const bool sent = Send(1, setup, sizeof(setup) - 1, 3000);
        mbedtls_platform_zeroize(setup, sizeof(setup));
        if (!sent) return Fail("setup_send_failed");
        const auto deadline = esp_timer_get_time() + 20000000;
        while (!result.connection.setup_ack && esp_timer_get_time() < deadline) if (!Pump(100)) return false;
        return result.connection.setup_ack || Fail("setup_timeout");
    }
    ~Conversation() {
        if (ws_) { esp_transport_close(ws_); esp_transport_destroy(ws_); }
        if (tls_) esp_transport_destroy(tls_);
        mbedtls_platform_zeroize(scratch_, sizeof(scratch_));
        mbedtls_platform_zeroize(control_, sizeof(control_));
    }
    bool Alive() {
        if (CompanionSessionProbe::QuestionCancelled()) return Fail("cancelled");
        if (!WifiManager::GetInstance().IsConnected()) return Fail("wifi_lost");
        return esp_timer_get_time() < session_deadline_ || Fail("session_timeout");
    }
    bool Turn(companion_question::Pcm& input, unsigned number) {
        if (!Alive()) return false;
        if (!input.data() || number == 0 || number > companion_followup::kMaxTurns) return Fail("invalid_input");
        reply_.Clear();
        auto& turn = result.turns[number - 1];
        CompanionSessionProbe::SetConversationStage(CompanionSessionProbe::VoiceStage::Connecting, number);
        const auto upload_deadline = esp_timer_get_time() + 12000000;
        for (std::size_t offset = 0; offset < companion_question::kSamples; offset += companion_question::kChunkSamples) {
            if (!Alive()) return false;
            if (esp_timer_get_time() >= upload_deadline) return Fail("audio_upload_timeout");
            const auto count = std::min(companion_question::kChunkSamples, companion_question::kSamples - offset);
            const auto size = companion_question::EncodeFrame(input.data() + offset, count, upload_.data, upload_.size);
            if (!size || !Send(1, upload_.data, size, 1000)) return Fail("audio_send_failed");
            mbedtls_platform_zeroize(upload_.data, upload_.size);
            turn.input_bytes += count * 2; ++turn.input_chunks;
            const auto next = esp_timer_get_time() + companion_question::kChunkMs * 1000;
            while (esp_timer_get_time() < next) {
                if (!Pump(static_cast<int>(std::max<int64_t>(1, (next - esp_timer_get_time()) / 1000)))) return false;
            }
        }
        char end[sizeof(companion_question::kEnd)];
        std::memcpy(end, companion_question::kEnd, sizeof(end));
        const bool ended = Send(1, end, sizeof(end) - 1, 1000);
        mbedtls_platform_zeroize(end, sizeof(end)); input.Clear();
        if (!ended) return Fail("audio_end_failed");
        turn.input_ended = true;
        ESP_LOGI(kTag, "INPUT turn=%u bytes=%u chunks=%u stream_ended=1 input_cleared=1", number, turn.input_bytes, turn.input_chunks);
        const auto deadline = esp_timer_get_time() + 30000000;
        while (!reply_.state.turn_complete && esp_timer_get_time() < deadline) if (!Pump(100)) return false;
        if (!reply_.state.turn_complete || !reply_.state.bytes) return Fail("turn_timeout");
        turn.turn_complete = true; turn.audio_bytes = reply_.state.bytes; turn.audio_chunks = reply_.state.chunks;
        turn.duration_ms = turn.audio_bytes / 48; turn.rms = reply_.rms(); turn.peak = reply_.state.peak; turn.clipped = reply_.state.clipped;
        CompanionSessionProbe::SetConversationStage(CompanionSessionProbe::VoiceStage::Speaking, number);
        Playback playback{reply_.samples(), reply_.state.bytes / 2};
        const auto started = esp_timer_get_time();
        if (xTaskCreate([](void* context) {
                auto& playback = *static_cast<Playback*>(context);
                playback.ok = Application::GetInstance().GetAudioService().PlayDiagnosticPcm(playback.pcm, playback.samples, playback.played);
                playback.done.store(true, std::memory_order_release); vTaskDelete(nullptr);
            }, "conversation_pcm", 4096, &playback, 3, nullptr) != pdPASS) return Fail("playback_task_failed");
        // The helper only queues PCM to the existing output task. Keep this
        // transport owner pumping ping/close/metadata while the speaker runs.
        bool healthy = true;
        while (!playback.done.load(std::memory_order_acquire)) {
            if (healthy) healthy = Pump(50);
            else vTaskDelay(pdMS_TO_TICKS(10));
        }
        turn.played_samples = playback.played;
        const auto settle = esp_timer_get_time() + 150000;
        while (healthy && esp_timer_get_time() < settle) healthy = Pump(20);
        turn.playback_ms = (esp_timer_get_time() - started) / 1000;
        if (!healthy) return false;
        if (!playback.ok || turn.played_samples * 2 != turn.audio_bytes) return Fail("playback_failed");
        ++result.completed_turns;
        ESP_LOGI(kTag, "TURN turn=%u complete=1 audio_bytes=%u chunks=%u sample_rate=24000 played_samples=%u duration_ms=%u rms=%u peak=%u clipped=%u playback_ms=%u",
            number, turn.audio_bytes, turn.audio_chunks, turn.played_samples, turn.duration_ms, turn.rms, turn.peak, turn.clipped, turn.playback_ms);
        return true;
    }
    std::unique_ptr<companion_question::Pcm> Followup(unsigned after_turn, bool fixture) {
        ++result.followup_captures;
        std::unique_ptr<companion_question::Pcm> input;
#if COMPANION_QUESTION_FIXTURE
        if (fixture) {
            CompanionSessionProbe::SetConversationStage(CompanionSessionProbe::VoiceStage::FollowupListening, after_turn);
            input = MakeFollowupFixture(after_turn >= companion_followup::kMaxTurns);
            ESP_LOGI(kTag, "FIXTURE_CAPTURE after_turn=%u microphone=0", after_turn);
        } else
#endif
        {
            (void)fixture;
            CompanionSessionProbe::RequestFollowupCapture();
            const auto deadline = esp_timer_get_time() + 8000000;
            std::uint32_t pending_id = 0;
            while (esp_timer_get_time() < deadline) {
                if (!Pump(50)) return nullptr;
                if (!pending_id) pending_id = CompanionSessionProbe::TakeFollowupCaptureId();
                // Finish any late metadata message under the previous turn's
                // completed validator before resetting it for a new answer.
                if (pending_id && !frames_.active() && frames_.message_size() == 0) {
                    input = Application::GetInstance().GetAudioService().TakeQuestionAudio(pending_id); break;
                }
            }
        }
        if (!input || !input->data()) { Fail("followup_capture_unavailable"); return nullptr; }
        struct Vad { vad_handle_t handle = vad_create(VAD_MODE_3); ~Vad() { if (handle) vad_destroy(handle); } } vad;
        if (!vad.handle) { Fail("vad_allocation_failed"); return nullptr; }
        companion_followup::SpeechGate gate;
        int16_t frame[companion_followup::kFrameSamples];
        for (unsigned offset = 0; offset < companion_question::kSamples; offset += companion_followup::kFrameSamples) {
            std::memcpy(frame, input->data() + offset, sizeof(frame));
            gate.Feed(vad_process(vad.handle, frame, 16000, companion_followup::kFrameMs) == VAD_SPEECH);
        }
        mbedtls_platform_zeroize(frame, sizeof(frame));
        ESP_LOGI(kTag, "VAD after_turn=%u frames=%u speech_frames=%u longest=%u accepted=%d mode=3", after_turn,
            gate.frames(), gate.positives(), gate.longest(), gate.speech());
        if (!Alive()) return nullptr;
        if (!gate.valid()) { Fail("invalid_vad_frames"); return nullptr; }
        if (!gate.speech()) {
            ++result.quiet_discards; input->Clear(); result.reason = "quiet_timeout";
            ESP_LOGI(kTag, "DISCARD after_turn=%u reason=quiet_timeout input_cleared=1 audio_uploaded=0", after_turn);
            return nullptr;
        }
        if (!companion_followup::CanUploadFollowup(after_turn, gate)) {
            input->Clear(); result.reason = "turn_limit";
            ESP_LOGI(kTag, "DISCARD after_turn=%u reason=turn_limit input_cleared=1 audio_uploaded=0", after_turn);
            return nullptr;
        }
        return input;
    }
    bool Close() {
        if (!ws_ || closed_) return closed_;
        char close[] = {0x03, static_cast<char>(0xe8)};
        result.connection.close_sent = Send(8, close, sizeof(close), 1000);
        if (!result.connection.close_sent) return Fail("close_send_failed");
        const auto deadline = esp_timer_get_time() + 3000000;
        while (!closed_ && esp_timer_get_time() < deadline) if (!Pump(100, true)) return false;
        if (!closed_) return Fail("close_timeout");
        return result.connection.close_code == 0 || result.connection.close_code == 1000 || result.connection.close_code == 1001 || Fail("unexpected_close_code");
    }
private:
    PrivateBuffer path_{companion_gemini::kPathCapacity}, message_{companion_voice::kMessageLimit + 1}, upload_{companion_question::kFrameCapacity};
    CompanionGeminiVoice::Reply reply_;
    companion_gemini::FrameTracker frames_{companion_voice::kMessageLimit};
    esp_transport_handle_t tls_ = nullptr, ws_ = nullptr;
    char scratch_[512]{}, control_[125]{};
    std::int64_t session_deadline_ = esp_timer_get_time() + companion_followup::kSessionLimitUs;
    std::size_t response_bytes_ = 0, close_drain_ = 0;
    unsigned response_messages_ = 0;
    bool closed_ = false;
    bool Fail(const char* reason) { result.reason = reason; return false; }
    bool Send(unsigned opcode, char* data, std::size_t size, int timeout) {
        return esp_transport_ws_send_raw(ws_, static_cast<ws_transport_opcodes_t>(opcode | 0x80), data, size, timeout) == static_cast<int>(size);
    }
    bool Pump(int timeout_ms, bool closing = false) {
        if (!closing && !Alive()) return false;
        const int count = esp_transport_read(ws_, scratch_, sizeof(scratch_), frames_.active() ? 1000 : std::max(1, timeout_ms));
        if (count < 0) return Fail("websocket_read_failed");
        const auto opcode = esp_transport_ws_get_read_opcode(ws_);
        if (opcode == WS_TRANSPORT_OPCODES_NONE) return count == 0 || Fail("invalid_frame_state");
        const int length = esp_transport_ws_get_read_payload_len(ws_);
        if (!count && length > 0) return Fail("partial_frame_timeout");
        if (length < 0 || (!frames_.active() && !frames_.Begin(opcode, esp_transport_ws_get_fin_flag(ws_), length)) ||
            frames_.opcode() != static_cast<unsigned>(opcode)) return Fail("invalid_or_oversized_frame");
        const auto offset = frames_.control() ? frames_.control_size() : frames_.message_size();
        const auto capacity = frames_.control() ? sizeof(control_) : companion_voice::kMessageLimit;
        if (static_cast<std::size_t>(count) > capacity - offset) return Fail("oversized_message");
        if (count) std::memcpy((frames_.control() ? control_ : message_.data) + offset, scratch_, count);
        const auto piece = frames_.Append(count);
        if (piece == companion_gemini::PieceResult::Invalid) return Fail("invalid_frame_length");
        if (piece == companion_gemini::PieceResult::Control) {
            const auto size = frames_.control_size();
            if (frames_.opcode() == 9 && !Send(10, control_, size, 1000)) return Fail("pong_send_failed");
            if (frames_.opcode() == 8) {
                result.connection.close_code = size >= 2 ? (static_cast<unsigned char>(control_[0]) << 8) | static_cast<unsigned char>(control_[1]) : 0;
                if (!result.connection.close_sent) { Send(8, control_, size, 1000); return Fail("unexpected_server_close"); }
                closed_ = result.connection.close_ack = true;
            }
        } else if (piece == companion_gemini::PieceResult::Message) {
            response_bytes_ += frames_.message_size();
            if (++response_messages_ > 512 || response_bytes_ > 4 * 1024 * 1024) return Fail("response_limit");
            bool ok = true;
            if (result.connection.close_sent) {
                close_drain_ += frames_.message_size();
                ok = close_drain_ <= companion_voice::kMessageLimit;
                if (!ok) Fail("oversized_close_drain");
            } else if (!result.connection.setup_ack) {
                ok = companion_gemini::IsSetupComplete(std::string_view(message_.data, frames_.message_size()));
                if (ok) { result.connection.setup_ack = true; result.connection.ack_bytes = frames_.message_size(); }
                else Fail("unexpected_setup_response");
            } else {
                ok = reply_.Consume(std::string_view(message_.data, frames_.message_size()), true);
                if (!ok) Fail(reply_.reason);
            }
            mbedtls_platform_zeroize(message_.data, frames_.message_size()); frames_.ResetMessage();
            if (!ok) return false;
        }
        return true;
    }
};
}  // namespace

namespace CompanionConversation {
Result Run(std::string_view temporary, companion_question::Pcm& first, bool fixture) {
    Conversation conversation;
    if (!conversation.Connect(temporary)) { first.Clear(); return conversation.result; }
    companion_question::Pcm* input = &first;
    std::unique_ptr<companion_question::Pcm> next;
    for (unsigned turn = 1; turn <= companion_followup::kMaxTurns; ++turn) {
        if (!conversation.Turn(*input, turn)) { input->Clear(); conversation.Close(); return conversation.result; }
        next = conversation.Followup(turn, fixture);
        if (!next) break;
        input = next.get();
    }
    const bool clean_end = !std::strcmp(conversation.result.reason, "quiet_timeout") || !std::strcmp(conversation.result.reason, "turn_limit");
    const bool closed = conversation.Close();
    conversation.result.ok = clean_end && closed && conversation.result.completed_turns > 0;
    conversation.result.connection.ok = conversation.result.ok;
    return conversation.result;
}
}  // namespace CompanionConversation
