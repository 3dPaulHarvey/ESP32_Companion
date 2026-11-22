#include "afe_wake_word.h"
#include <esp_log.h>
#include <cstring>
#include <cstdlib>
#include <sstream>

#define DETECTION_RUNNING_EVENT 1
#define TAG "AfeWakeWord"

AfeWakeWord::AfeWakeWord() {
    event_group_ = xEventGroupCreate();
}

AfeWakeWord::~AfeWakeWord() {
    if (detection_task_) vTaskDelete(detection_task_);
    if (afe_data_) afe_iface_->destroy(afe_data_);
    if (event_group_) vEventGroupDelete(event_group_);
}

bool AfeWakeWord::Initialize(srmodel_list_t* models_list) {
    models_ = models_list;
    if (!event_group_ || !models_ || models_->num <= 0) {
        ESP_LOGE(TAG, "Missing local wake model");
        return false;
    }
    char* wakenet_model = nullptr;
    for (int i = 0; i < models_->num; ++i) {
        ESP_LOGI(TAG, "Model %d: %s", i, models_->model_name[i]);
        if (std::strcmp(models_->model_name[i], "wn9_hiesp") == 0) {
            wakenet_model = models_->model_name[i];
        }
    }
    if (!wakenet_model) {
        ESP_LOGE(TAG, "Required wn9_hiesp wake model is missing");
        return false;
    }
    char* words = esp_srmodel_get_wake_words(models_, wakenet_model);
    if (!words) return false;
    wake_words_.clear();
    std::stringstream stream(words);
    std::free(words);
    std::string word;
    while (std::getline(stream, word, ';')) {
        if (!word.empty()) wake_words_.push_back(word);
    }
    if (wake_words_.empty()) return false;

    afe_config_t* config = afe_config_init("M", models_, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!config) return false;
    config->aec_init = false;
    config->aec_mode = AEC_MODE_SR_HIGH_PERF;
    config->afe_perferred_core = 1;
    config->afe_perferred_priority = 1;
    config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_iface_ = esp_afe_handle_from_config(config);
    if (afe_iface_) afe_data_ = afe_iface_->create_from_config(config);
    afe_config_free(config);
    if (!afe_data_) return false;
    const BaseType_t created = xTaskCreate([](void* arg) {
        static_cast<AfeWakeWord*>(arg)->AudioDetectionTask();
        vTaskDelete(nullptr);
    }, "audio_detection", 4096, this, 3, &detection_task_);
    if (created != pdPASS) {
        afe_iface_->destroy(afe_data_);
        afe_data_ = nullptr;
        return false;
    }
    return true;
}

void AfeWakeWord::OnWakeWordDetected(std::function<void(const std::string&)> callback) {
    wake_word_detected_callback_ = callback;
}

void AfeWakeWord::Start() {
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void AfeWakeWord::Stop() {
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    if (afe_data_) afe_iface_->reset_buffer(afe_data_);
}

void AfeWakeWord::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ && (xEventGroupGetBits(event_group_) & DETECTION_RUNNING_EVENT)) {
        afe_iface_->feed(afe_data_, data.data());
    }
}

size_t AfeWakeWord::GetFeedSize() {
    return afe_data_ ? afe_iface_->get_feed_chunksize(afe_data_) : 0;
}

void AfeWakeWord::AudioDetectionTask() {
    ESP_LOGI(TAG, "Audio detection task started, feed size: %d fetch size: %d",
        afe_iface_->get_feed_chunksize(afe_data_), afe_iface_->get_fetch_chunksize(afe_data_));
    while (true) {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);
        auto result = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if (!result || result->ret_value == ESP_FAIL) continue;
        if (result->wakeup_state == WAKENET_DETECTED) {
            const int index = result->wakenet_model_index - 1;
            if (index < 0 || static_cast<size_t>(index) >= wake_words_.size()) continue;
            Stop();
            if (wake_word_detected_callback_) wake_word_detected_callback_(wake_words_[index]);
        }
    }
}
