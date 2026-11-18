#include "hardware_diagnostics.h"

#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "local_companion_cycle.h"
#include "audio/local_pcm_capture.h"
#include "audio/local_capture_config.h"
#include "audio/calibration_lease.h"
#if CONFIG_COMPANION_SESSION_PROBE
#include "companion/session_probe.h"
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include <esp_flash.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <wifi_manager.h>

namespace {
constexpr char kTag[] = "HardwareDiagnostics";
#if CONFIG_COMPANION_SESSION_PROBE
constexpr char kCloudMode[] = "manual-session-probe";
#else
constexpr char kCloudMode[] = "disabled";
#endif
constexpr double kPi = 3.14159265358979323846;
ESP_EVENT_DEFINE_BASE(DIAGNOSTIC_WIFI_EVENT);

void PrintMemory() {
    ESP_LOGI(kTag, "HEAP internal_free=%u internal_min=%u internal_largest=%u psram_total=%u psram_free=%u",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void PrintFlashLayout() {
    uint32_t flash_size = 0;
    ESP_ERROR_CHECK(esp_flash_get_size(nullptr, &flash_size));
    ESP_LOGI(kTag, "FLASH bytes=%lu", static_cast<unsigned long>(flash_size));
    for (const char* name : {"nvs", "otadata", "phy_init", "ota_0", "ota_1", "assets"}) {
        auto partition = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, name);
        if (partition != nullptr) {
            ESP_LOGI(kTag, "PARTITION %s offset=0x%lx size=0x%lx", name,
                static_cast<unsigned long>(partition->address), static_cast<unsigned long>(partition->size));
        } else {
            ESP_LOGE(kTag, "PARTITION missing: %s", name);
        }
    }
}

// Run before AudioService::Start(), so this is the sole speaker writer.
// Keep the saved volume intact; low PCM amplitude limits test loudness.
void PlayStartupTone(AudioCodec& codec) {
    ESP_LOGI(kTag, "SPEAKER tone: 600Hz then 900Hz, 24kHz PCM, low amplitude");
    constexpr int sample_count = 6000;  // 250 ms at this board's 24 kHz rate.
    std::vector<int16_t> pcm(sample_count);
    for (int frequency : {600, 900}) {
        for (int i = 0; i < sample_count; ++i) {
            const double fade = std::fmin(1.0, std::fmin(i / 240.0, (sample_count - 1 - i) / 240.0));
            pcm[i] = static_cast<int16_t>(2000.0 * fade * std::sin(2.0 * kPi * frequency * i / 24000.0));
        }
        codec.OutputData(pcm);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(kTag, "SPEAKER tone complete; confirm audible quality at the device");
}

void SetFace(Display& display, FaceState face, const char* label, const char* status = nullptr) {
    // Make the active capture window unambiguous on the small OLED.
    // SetStatus takes its own display lock.
    display.SetStatus(status ? status : (face == FaceState::Listening ? "LISTENING"
        : (face == FaceState::Speaking ? "CUE" : "READY")));
    // The existing face timer runs on LVGL's task; serialize the state update.
    DisplayLockGuard guard(&display);
    display.SetFaceState(face);
    ESP_LOGI(kTag, "FACE %s", label);
}

void PrintHelp() {
    ESP_LOGI(kTag, "COMMANDS h=help n=synthetic-cycle x=cancel-capture d=face-cycle t=local-cue w=WiFi-start o=WiFi-off r=disconnect/retry s=status");
}

void PrintCapturePolicy() {
    ESP_LOGI("LocalCompanion", "CAPTURE_POLICY samples=%u bytes=%u duration_ms=%u sample_rate=16000 worker_timeout_ms=%u pcm_shift=%u",
        static_cast<unsigned>(companion_capture::kSamples), static_cast<unsigned>(companion_capture::kBytes),
        companion_capture::kDurationMs, companion_capture::kWorkerTimeoutMs, companion_capture::kPcmShift);
#if CONFIG_COMPANION_MICROPHONE_PREP
    ESP_LOGI("MicrophonePrep", "FEATURE enabled=1 capture_only=1 wake_shift=12 audio_upload=0");
    ESP_LOGI("MicrophonePrep", "CALIBRATOR_PROTOCOL version=1 lease_ms=10000 recording_event=1");
#endif
}

const char* WifiEventName(WifiEvent event) {
    switch (event) {
        case WifiEvent::Scanning: return "scanning";
        case WifiEvent::Connecting: return "connecting";
        case WifiEvent::Connected: return "connected";
        case WifiEvent::Disconnected: return "disconnected";
        case WifiEvent::ConfigModeEnter: return "config-enter";
        case WifiEvent::ConfigModeExit: return "config-exit";
    }
    return "unknown";
}
}  // namespace

[[noreturn]] void RunHardwareDiagnostics() {
    // IDF's nonblocking VFS read checks the driver's RX ring buffer even when
    // its default polling backend is selected. Install the RX driver explicitly.
    auto usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI(kTag, "MODE local diagnostics; cloud=%s; WiFi=off", kCloudMode);
    auto& board = Board::GetInstance();
    auto& display = *board.GetDisplay();
    auto& codec = *board.GetAudioCodec();
    // Local asset loading binds models to this existing service instance.
    auto& audio = Application::GetInstance().GetAudioService();
    audio.Initialize(&codec);
    ESP_LOGI(kTag, "AUDIO input=%dHz/%dch output=%dHz/%dch reference=%d",
        codec.input_sample_rate(), codec.input_channels(), codec.output_sample_rate(),
        codec.output_channels(), codec.input_reference());
    ESP_ERROR_CHECK(codec.input_sample_rate() == 16000 && codec.output_sample_rate() == 24000
        ? ESP_OK : ESP_ERR_INVALID_STATE);
    PrintFlashLayout();

    SetFace(display, FaceState::Idle, "idle/blink");
    vTaskDelay(pdMS_TO_TICKS(2000));
    SetFace(display, FaceState::Listening, "listening");
    vTaskDelay(pdMS_TO_TICKS(2000));
    SetFace(display, FaceState::Speaking, "speaking");
    PlayStartupTone(codec);
    vTaskDelay(pdMS_TO_TICKS(1200));
    SetFace(display, FaceState::Idle, "idle/blink");

    std::atomic<uint32_t> wake_count{0};
    AudioServiceCallbacks callbacks;
    callbacks.on_wake_word_detected = [&wake_count](const std::string&) {
        // Called from the AFE fetch task: let the diagnostic loop own re-arming/UI.
        wake_count.fetch_add(1, std::memory_order_relaxed);
    };
    audio.SetCallbacks(callbacks);
    const bool applied = Assets::GetInstance().Apply();
    if (!applied || !audio.IsAfeWakeWord()) {
        ESP_LOGE(kTag, "FAIL local wake model missing/invalid; flash generated English assets");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    audio.Start();
    audio.EnableWakeWordDetection(true);
    if (!audio.IsWakeWordRunning()) {
        ESP_LOGE(kTag, "FAIL wake detector did not initialize");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    setvbuf(stdin, nullptr, _IONBF, 0);
    const int input_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    ESP_ERROR_CHECK(input_flags >= 0 && fcntl(STDIN_FILENO, F_SETFL, input_flags | O_NONBLOCK) == 0
        ? ESP_OK : ESP_FAIL);
    PrintHelp();
    PrintMemory();
    PrintCapturePolicy();
    ESP_LOGI(kTag, "WAKE_READY count=0 phrase=Hi,ESP WiFi=off cloud=%s", kCloudMode);
#if CONFIG_COMPANION_SESSION_PROBE
    CompanionSessionProbe::Initialize();
#endif

    auto& wifi = WifiManager::GetInstance();
    bool wifi_active = false;
    bool wifi_connected = false;
    bool wifi_stop_pending = false;
    std::atomic<bool> wifi_stop_complete{false};
    esp_event_handler_instance_t stop_handler = nullptr;
    uint32_t handled_wakes = 0;
    LocalCompanionCycle cycle(companion_capture::kControllerTimeoutUs);
    uint32_t voice_cycles = 0;
    uint32_t serial_cycles = 0;
    const char* cycle_source = "none";
    int face_step = 0;
    int64_t face_deadline = 0;
    int64_t memory_deadline = esp_timer_get_time() + 10000000;
    bool capture_request_pending = false;
    bool capture_start_failed = false;
    bool wake_armed = true;
    MicrophoneCalibrationLease calibration_lease;
    bool manual_voice_active = false;
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    bool manual_voice_speaking = false;
#endif
    uint32_t capture_request_id = 0;
    uint32_t completed_reported = 0;
    const char* abort_reason = "none";
    ESP_LOGI("LocalCompanion", "STATE IDLE mode=bounded-pcm-capture capture=%s cloud=%s", companion_capture::kLabel, kCloudMode);

    auto arm_wake = [&]() {
        if (!capture_request_pending && !manual_voice_active && !calibration_lease.held() && !wake_armed) {
            audio.EnableWakeWordDetection(true);
            wake_armed = true;
            ESP_LOGI(kTag, "WAKE_READY count=%lu WiFi=%s cloud=%s",
                static_cast<unsigned long>(handled_wakes), wifi_active ? "on" : "off", kCloudMode);
        }
    };

    auto on_transition = [&](LocalCompanionState previous) {
        ESP_LOGI("LocalCompanion", "STATE %s -> %s cycle=%lu source=%s",
            LocalCompanionCycle::StateName(previous), LocalCompanionCycle::StateName(cycle.state()),
            static_cast<unsigned long>(cycle.started()), cycle_source);
        switch (cycle.state()) {
            case LocalCompanionState::WakeDetected:
                audio.EnableWakeWordDetection(false);
                wake_armed = false;
                face_step = 0;
                SetFace(display, FaceState::Listening, "wake-detected");
                break;
            case LocalCompanionState::Listening:
                SetFace(display, FaceState::Listening, "listening/pcm-capture");
                capture_request_id = cycle.started();
                capture_request_pending = audio.BeginLocalCapture(capture_request_id);
                capture_start_failed = !capture_request_pending;
                if (capture_request_pending) {
                    ESP_LOGI("LocalCompanion", "CAPTURE_BEGIN request=%lu target_samples=%u bytes=%u sample_rate=16000 warmup_samples=3200",
                        static_cast<unsigned long>(capture_request_id), static_cast<unsigned>(companion_capture::kSamples),
                        static_cast<unsigned>(companion_capture::kBytes));
                } else {
                    ESP_LOGE("LocalCompanion", "CAPTURE_START_FAILED request=%lu", static_cast<unsigned long>(capture_request_id));
                }
                break;
            case LocalCompanionState::Speaking:
                SetFace(display, FaceState::Speaking, "speaking/local-cue");
                audio.PlaySound(Lang::Sounds::OGG_POPUP);
                break;
            case LocalCompanionState::Idle:
                SetFace(display, FaceState::Idle, "idle/blink");
                if (capture_request_pending) { audio.CancelLocalCapture(capture_request_id); }
                if (cycle.completed() != completed_reported) {
                    completed_reported = cycle.completed();
                    ESP_LOGI("LocalCompanion", "CYCLE_COMPLETE cycle=%lu completed=%lu source=%s voice_started=%lu serial_started=%lu aborted=%lu",
                        static_cast<unsigned long>(cycle.started()), static_cast<unsigned long>(cycle.completed()),
                        cycle_source, static_cast<unsigned long>(voice_cycles), static_cast<unsigned long>(serial_cycles),
                        static_cast<unsigned long>(cycle.aborted()));
                } else {
                    ESP_LOGW("LocalCompanion", "CYCLE_ABORTED cycle=%lu aborted=%lu source=%s reason=%s",
                        static_cast<unsigned long>(cycle.started()), static_cast<unsigned long>(cycle.aborted()), cycle_source, abort_reason);
                }
                arm_wake();
                break;
        }
    };
    auto begin_cycle = [&](int64_t now, const char* source) {
        const auto previous = cycle.state();
        if (manual_voice_active || (source[0] == 'v' && calibration_lease.held()) || capture_request_pending || !cycle.BeginWake(now)) {
            ESP_LOGI("LocalCompanion", "WAKE_IGNORED source=%s state=%s", source,
                LocalCompanionCycle::StateName(cycle.state()));
            return;
        }
        cycle_source = source;
        abort_reason = "none";
        if (source[0] == 'v') { ++voice_cycles; } else { ++serial_cycles; }
        on_transition(previous);
    };

    while (true) {
        const int64_t now = esp_timer_get_time();
#if CONFIG_COMPANION_SESSION_PROBE
        CompanionSessionProbe::Poll(now);
#endif
#if CONFIG_COMPANION_MICROPHONE_PREP
        if (calibration_lease.Expire(now)) {
            CompanionSessionProbe::SetCalibrationGuard(false);
            if (cycle.state() == LocalCompanionState::Idle) arm_wake();
            ESP_LOGI("MicrophonePrep", "CALIBRATOR_RELEASE reason=lease_expired wake_armed=%d", wake_armed);
        }
#endif
        if (wifi_stop_pending && wifi_stop_complete.exchange(false, std::memory_order_acquire)) {
            wifi_active = false;
            wifi_stop_pending = false;
            ESP_LOGI(kTag, "WIFI off; wake remains local; cloud=%s", kCloudMode);
        }
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
        if (manual_voice_active) {
            const auto stage = CompanionSessionProbe::GetVoiceStage();
            if (stage == CompanionSessionProbe::VoiceStage::Speaking && !manual_voice_speaking) {
                manual_voice_speaking = true;
                SetFace(display, FaceState::Speaking, "speaking/Gemini-greeting", "SPEAKING");
                ESP_LOGI("CompanionVoice", "UI state=SPEAKING wake_armed=0");
            }
            if (stage == CompanionSessionProbe::VoiceStage::Finished && !audio.IsDiagnosticPcmBusy()) {
                manual_voice_active = false; manual_voice_speaking = false;
                CompanionSessionProbe::AcknowledgeVoice();
                SetFace(display, FaceState::Idle, "idle/blink");
                arm_wake();
                ESP_LOGI("CompanionVoice", "UI state=READY wake_armed=1");
            }
        }
#endif
        LocalPcmCaptureResult capture_result;
        if (audio.PollLocalCaptureResult(capture_result)) {
            if (capture_request_pending && capture_result.request_id == capture_request_id) {
                capture_request_pending = false;  // Worker has wiped/freed its buffer before publication.
            }
            if (cycle.state() == LocalCompanionState::Listening && capture_result.request_id == cycle.started()) {
                const auto previous_capture_state = cycle.state();
                const bool valid = capture_result.success && capture_result.status == LocalPcmCaptureStatus::Success
                    && capture_result.samples == companion_capture::kSamples && capture_result.sample_rate == 16000;
                if (valid && cycle.CaptureFinished(now)) {
                    ESP_LOGI("LocalCompanion", "CAPTURE_COMPLETE request=%lu samples=%lu bytes=%lu duration_ms=%u sample_rate=16000 elapsed_ms=%lu rms=%lu peak=%ld clipped=%lu",
                        static_cast<unsigned long>(capture_result.request_id), static_cast<unsigned long>(capture_result.samples),
                        static_cast<unsigned long>(capture_result.samples * sizeof(int16_t)),
                        companion_capture::kDurationMs,
                        static_cast<unsigned long>(capture_result.elapsed_ms), static_cast<unsigned long>(capture_result.rms),
                        static_cast<long>(capture_result.peak), static_cast<unsigned long>(capture_result.clipped));
#if CONFIG_COMPANION_MICROPHONE_PREP
                    const auto& microphone = capture_result.microphone;
                    ESP_LOGI("MicrophonePrep", "HEADROOM request=%lu samples=%lu pcm_shift=%u raw_peak=%lu legacy_clipped=%lu raw_near_full_scale=%lu suggested_shift=%u dc_mean=%ld ac_rms=%lu",
                        static_cast<unsigned long>(capture_result.request_id), static_cast<unsigned long>(microphone.samples),
                        microphone.pcm_shift, static_cast<unsigned long>(microphone.raw_peak),
                        static_cast<unsigned long>(microphone.legacy_clipped), static_cast<unsigned long>(microphone.raw_near_full_scale),
                        microphone.suggested_shift, static_cast<long>(microphone.dc_mean), static_cast<unsigned long>(microphone.ac_rms));
                    for (unsigned i = 0; i < microphone.window_samples.size(); ++i) {
                        ESP_LOGI("MicrophonePrep", "WINDOW request=%lu second=%u samples=%lu rms=%lu peak=%lu clipped=%lu",
                            static_cast<unsigned long>(capture_result.request_id), i + 1,
                            static_cast<unsigned long>(microphone.window_samples[i]), static_cast<unsigned long>(microphone.window_rms[i]),
                            static_cast<unsigned long>(microphone.window_peak[i]), static_cast<unsigned long>(microphone.window_clipped[i]));
                    }
#endif
                    on_transition(previous_capture_state);
                } else {
                    abort_reason = valid ? "controller-timeout" : (capture_result.success ? "invalid-result" : LocalPcmCaptureStatusName(capture_result.status));
                    ESP_LOGW("LocalCompanion", "CAPTURE_FAILED request=%lu samples=%lu status=%s",
                        static_cast<unsigned long>(capture_result.request_id), static_cast<unsigned long>(capture_result.samples), abort_reason);
                    if (cycle.Abort()) { on_transition(previous_capture_state); }
                }
            } else {
                ESP_LOGW("LocalCompanion", "CAPTURE_RESULT_DISCARDED request=%lu state=%s status=%s",
                    static_cast<unsigned long>(capture_result.request_id), LocalCompanionCycle::StateName(cycle.state()),
                    LocalPcmCaptureStatusName(capture_result.status));
                if (cycle.state() == LocalCompanionState::Idle) { arm_wake(); }
            }
        }
        const uint32_t observed_wakes = wake_count.load(std::memory_order_relaxed);
        if (observed_wakes != handled_wakes) {
            handled_wakes = observed_wakes;
            ESP_LOGI(kTag, "WAKE_DETECTED count=%lu WiFi=%s cloud=%s",
                static_cast<unsigned long>(handled_wakes), wifi_active ? "on" : "off", kCloudMode);
            begin_cycle(now, "voice");
        }
        const auto previous = cycle.state();
        if (cycle.Tick(now)) {
            if (previous == LocalCompanionState::Listening) { abort_reason = "controller-watchdog"; }
            on_transition(previous);
        }
        if (capture_start_failed) {
            capture_start_failed = false;
            abort_reason = "capture-rejected";
            const auto failed_state = cycle.state();
            if (cycle.Abort()) { on_transition(failed_state); }
        }
        if (!manual_voice_active && face_step != 0 && now >= face_deadline && cycle.state() == LocalCompanionState::Idle) {
            if (face_step == 1) { SetFace(display, FaceState::Listening, "listening/test"); }
            if (face_step == 2) { SetFace(display, FaceState::Speaking, "speaking/test"); }
            if (face_step == 3) { SetFace(display, FaceState::Idle, "idle/blink"); }
            face_step = face_step == 3 ? 0 : face_step + 1;
            face_deadline = now + 2000000;
        }

        char command = 0;
        if (read(STDIN_FILENO, &command, 1) == 1) {
#if CONFIG_COMPANION_SESSION_PROBE
            if (CompanionSessionProbe::HandleSerialByte(command, now)) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
#endif
            switch (command) {
#if CONFIG_COMPANION_MICROPHONE_PREP
                case '[':
                    if (cycle.state() == LocalCompanionState::Idle && !capture_request_pending && !manual_voice_active && !CompanionSessionProbe::IsBusy()) {
                        calibration_lease.Hold(now);
                        CompanionSessionProbe::SetCalibrationGuard(true);
                        audio.EnableWakeWordDetection(false); wake_armed = false;
                        face_step = 0;
                        ESP_LOGI("MicrophonePrep", "CALIBRATOR_CONTROL hold=1 lease_ms=10000");
                    } else ESP_LOGW("MicrophonePrep", "CALIBRATOR_REJECTED reason=busy");
                    break;
                case '=':
                    calibration_lease.Heartbeat(now);
                    break;
                case ']':
                    calibration_lease.Release();
                    CompanionSessionProbe::SetCalibrationGuard(false);
                    if (cycle.state() == LocalCompanionState::Idle) arm_wake();
                    ESP_LOGI("MicrophonePrep", "CALIBRATOR_CONTROL hold=0 wake_armed=%d", wake_armed);
                    break;
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
                case 'v':
                    if (calibration_lease.held() || manual_voice_active || cycle.state() != LocalCompanionState::Idle || capture_request_pending || face_step || !audio.IsIdle()) {
                        ESP_LOGW("CompanionVoice", "REQUEST_REJECTED reason=local_busy");
                    } else if (CompanionSessionProbe::RequestVoice()) {
                        manual_voice_active = true; manual_voice_speaking = false;
                        audio.EnableWakeWordDetection(false); wake_armed = false;
                        SetFace(display, FaceState::Listening, "connecting/Gemini-greeting", "CONNECTING");
                        ESP_LOGI("CompanionVoice", "UI state=CONNECTING wake_armed=0");
                    }
                    break;
#endif
                case 'h': PrintHelp(); break;
                case 'n': begin_cycle(now, "serial"); break;
                case 'x':
                    if (cycle.state() == LocalCompanionState::Listening && capture_request_pending) {
                        audio.CancelLocalCapture(capture_request_id);
                        ESP_LOGI("LocalCompanion", "CAPTURE_CANCEL_REQUEST request=%lu", static_cast<unsigned long>(capture_request_id));
                    } else {
                        ESP_LOGI("LocalCompanion", "COMMAND_IGNORED command=x state=%s", LocalCompanionCycle::StateName(cycle.state()));
                    }
                    break;
                case 's':
                    PrintMemory();
                    PrintCapturePolicy();
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
                    ESP_LOGI("CompanionVoice", "STATUS active=%d speaking=%d wake_armed=%d playback_busy=%d",
                        manual_voice_active, manual_voice_speaking, wake_armed, audio.IsDiagnosticPcmBusy());
#endif
                    ESP_LOGI(kTag, "STATUS wakes=%lu WiFi=%s connected=%d cloud=%s",
                        static_cast<unsigned long>(handled_wakes), wifi_active ? "on" : "off", wifi.IsConnected(), kCloudMode);
                    ESP_LOGI("LocalCompanion", "STATUS state=%s started=%lu completed=%lu voice_started=%lu serial_started=%lu aborted=%lu capture=%s pending=%d",
                        LocalCompanionCycle::StateName(cycle.state()), static_cast<unsigned long>(cycle.started()),
                        static_cast<unsigned long>(cycle.completed()), static_cast<unsigned long>(voice_cycles),
                        static_cast<unsigned long>(serial_cycles), static_cast<unsigned long>(cycle.aborted()), companion_capture::kLabel, capture_request_pending);
#if CONFIG_COMPANION_SESSION_PROBE
                    CompanionSessionProbe::PrintStatus();
#endif
                    break;
                case 'd':
                    if (!manual_voice_active && cycle.state() == LocalCompanionState::Idle && !capture_request_pending) {
                        SetFace(display, FaceState::Idle, "idle/test");
                        face_step = 1;
                        face_deadline = now + 2000000;
                    } else {
                        ESP_LOGI("LocalCompanion", "COMMAND_IGNORED command=d state=%s", LocalCompanionCycle::StateName(cycle.state()));
                    }
                    break;
                case 't':
                    if (!manual_voice_active && cycle.state() == LocalCompanionState::Idle && !capture_request_pending) {
                        audio.PlaySound(Lang::Sounds::OGG_POPUP);
                    } else {
                        ESP_LOGI("LocalCompanion", "COMMAND_IGNORED command=t state=%s", LocalCompanionCycle::StateName(cycle.state()));
                    }
                    break;
                case 'w': {
                    if (wifi_stop_pending) {
                        ESP_LOGW(kTag, "WIFI shutdown pending; wait for WIFI off");
                        break;
                    }
                    if (!wifi.IsInitialized()) {
                        WifiManagerConfig config;
                        config.language = "en-US";
                        config.station_scan_min_interval_seconds = 5;
                        config.station_scan_max_interval_seconds = 30;
                        ESP_ERROR_CHECK(wifi.Initialize(config) ? ESP_OK : ESP_FAIL);
                        wifi.SetEventCallback([](WifiEvent event) {
                            ESP_LOGI(kTag, "WIFI event=%s cloud=%s", WifiEventName(event), kCloudMode);
                        });
                        // The existing manager holds its mutex while unregistering Wi-Fi
                        // callbacks during StopStation(). Dispatch shutdown on their own
                        // default loop to avoid event-loop/manager lock inversion.
                        ESP_ERROR_CHECK(esp_event_handler_instance_register(DIAGNOSTIC_WIFI_EVENT, 0,
                            [](void* context, esp_event_base_t, int32_t, void*) {
                                WifiManager::GetInstance().StopStation();
                                static_cast<std::atomic<bool>*>(context)->store(true, std::memory_order_release);
                            }, &wifi_stop_complete, &stop_handler));
                    }
                    if (!wifi_active) {
                        wifi.StartStation();
                        wifi_active = true;
                        ESP_LOGI(kTag, "WIFI started with saved credentials; cloud=%s", kCloudMode);
                    }
                    break;
                }
                case 'o':
                    if (wifi_active && !wifi_stop_pending) {
                        wifi_stop_pending = true;
                        ESP_ERROR_CHECK(esp_event_post(DIAGNOSTIC_WIFI_EVENT, 0, nullptr, 0, pdMS_TO_TICKS(100)));
                    } else if (!wifi_active) {
                        ESP_LOGI(kTag, "WIFI already off; cloud=%s", kCloudMode);
                    }
                    break;
                case 'r':
                    if (wifi_active && !wifi_stop_pending && wifi.IsConnected()) {
                        ESP_LOGI(kTag, "WIFI intentional disconnect; testing automatic retry");
                        ESP_ERROR_CHECK(esp_wifi_disconnect());
                    } else {
                        ESP_LOGW(kTag, "WIFI retry test needs a connected station; use w first");
                    }
                    break;
                case '\n': case '\r': case ' ': break;
                default: ESP_LOGW(kTag, "Unknown command; use h"); break;
            }
        }

        const bool connected_now = wifi_active && wifi.IsConnected();
        if (connected_now != wifi_connected) {
            wifi_connected = connected_now;
            if (connected_now) {
                ESP_LOGI(kTag, "WIFI connected ip=%s rssi=%d cloud=%s",
                    wifi.GetIpAddress().c_str(), wifi.GetRssi(), kCloudMode);
            } else {
                ESP_LOGI(kTag, "WIFI disconnected; local audio/display/wake continue");
            }
        }
        if (now >= memory_deadline) {
            PrintMemory();
            memory_deadline = now + 10000000;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
