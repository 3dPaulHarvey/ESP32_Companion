#ifndef AFE_WAKE_WORD_H
#define AFE_WAKE_WORD_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_afe_sr_models.h>
#include <model_path.h>
#include <string>
#include <vector>
#include <functional>

// The fixed board uses one microphone and the bundled English wn9_hiesp model.
class AfeWakeWord {
public:
    AfeWakeWord();
    ~AfeWakeWord();
    bool Initialize(srmodel_list_t* models_list);
    void Feed(const std::vector<int16_t>& data);
    void OnWakeWordDetected(std::function<void(const std::string&)> callback);
    void Start();
    void Stop();
    size_t GetFeedSize();

private:
    srmodel_list_t* models_ = nullptr;  // Borrowed from the mapped assets.
    const esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    std::vector<std::string> wake_words_;
    EventGroupHandle_t event_group_;
    TaskHandle_t detection_task_ = nullptr;
    std::function<void(const std::string&)> wake_word_detected_callback_;
    void AudioDetectionTask();
};

#endif
