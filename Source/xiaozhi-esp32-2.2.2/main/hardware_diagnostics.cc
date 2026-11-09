#include "hardware_diagnostics.h"

#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"

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

void SetFace(Display& display, FaceState face, const char* label) {
    // The existing face timer runs on LVGL's task; serialize the state update.
    DisplayLockGuard guard(&display);
    display.SetFaceState(face);
    ESP_LOGI(kTag, "FACE %s", label);
}

void PrintHelp() {
    ESP_LOGI(kTag, "COMMANDS h=help d=face-cycle t=local-cue w=WiFi-start o=WiFi-off r=disconnect/retry s=status");
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
    ESP_LOGI(kTag, "MODE local diagnostics; cloud=disabled; WiFi=off");
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
    ESP_LOGI(kTag, "WAKE_READY count=0 phrase=Hi,ESP WiFi=off cloud=disabled");

    auto& wifi = WifiManager::GetInstance();
    bool wifi_active = false;
    bool wifi_connected = false;
    bool wifi_stop_pending = false;
    std::atomic<bool> wifi_stop_complete{false};
    esp_event_handler_instance_t stop_handler = nullptr;
    uint32_t handled_wakes = 0;
    int wake_phase = 0;
    int face_step = 0;
    int64_t face_deadline = 0;
    int64_t wake_deadline = 0;
    int64_t memory_deadline = esp_timer_get_time() + 10000000;

    while (true) {
        const int64_t now = esp_timer_get_time();
        if (wifi_stop_pending && wifi_stop_complete.exchange(false, std::memory_order_acquire)) {
            wifi_active = false;
            wifi_stop_pending = false;
            ESP_LOGI(kTag, "WIFI off; wake remains local; cloud=disabled");
        }
        const uint32_t observed_wakes = wake_count.load(std::memory_order_relaxed);
        if (observed_wakes != handled_wakes) {
            handled_wakes = observed_wakes;
            audio.EnableWakeWordDetection(false);
            face_step = 0;
            ESP_LOGI(kTag, "WAKE_DETECTED count=%lu WiFi=%s cloud=disabled",
                static_cast<unsigned long>(handled_wakes), wifi_active ? "on" : "off");
            SetFace(display, FaceState::Listening, "listening/local-wake");
            wake_phase = 1;
            wake_deadline = now + 400000;
        }
        if (wake_phase == 1 && now >= wake_deadline) {
            SetFace(display, FaceState::Speaking, "speaking/local-cue");
            audio.PlaySound(Lang::Sounds::OGG_POPUP);
            wake_phase = 2;
            wake_deadline = now + 800000;
        } else if (wake_phase == 2 && now >= wake_deadline) {
            SetFace(display, FaceState::Idle, "idle/blink");
            audio.EnableWakeWordDetection(true);
            wake_phase = 0;
            ESP_LOGI(kTag, "WAKE_READY count=%lu WiFi=%s cloud=disabled",
                static_cast<unsigned long>(handled_wakes), wifi_active ? "on" : "off");
        }
        if (face_step != 0 && now >= face_deadline && wake_phase == 0) {
            if (face_step == 1) { SetFace(display, FaceState::Listening, "listening/test"); }
            if (face_step == 2) { SetFace(display, FaceState::Speaking, "speaking/test"); }
            if (face_step == 3) { SetFace(display, FaceState::Idle, "idle/blink"); }
            face_step = face_step == 3 ? 0 : face_step + 1;
            face_deadline = now + 2000000;
        }

        char command = 0;
        if (read(STDIN_FILENO, &command, 1) == 1) {
            switch (command) {
                case 'h': PrintHelp(); break;
                case 's':
                    PrintMemory();
                    ESP_LOGI(kTag, "STATUS wakes=%lu WiFi=%s connected=%d cloud=disabled",
                        static_cast<unsigned long>(handled_wakes), wifi_active ? "on" : "off", wifi.IsConnected());
                    break;
                case 'd':
                    if (wake_phase == 0) {
                        SetFace(display, FaceState::Idle, "idle/test");
                        face_step = 1;
                        face_deadline = now + 2000000;
                    }
                    break;
                case 't': audio.PlaySound(Lang::Sounds::OGG_POPUP); break;
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
                            ESP_LOGI(kTag, "WIFI event=%s cloud=disabled", WifiEventName(event));
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
                        ESP_LOGI(kTag, "WIFI started with saved credentials; cloud=disabled");
                    }
                    break;
                }
                case 'o':
                    if (wifi_active && !wifi_stop_pending) {
                        wifi_stop_pending = true;
                        ESP_ERROR_CHECK(esp_event_post(DIAGNOSTIC_WIFI_EVENT, 0, nullptr, 0, pdMS_TO_TICKS(100)));
                    } else if (!wifi_active) {
                        ESP_LOGI(kTag, "WIFI already off; cloud=disabled");
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
                ESP_LOGI(kTag, "WIFI connected ip=%s rssi=%d cloud=disabled",
                    wifi.GetIpAddress().c_str(), wifi.GetRssi());
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
