#include "audio_codec.h"
#include <nvs.h>

#include <esp_log.h>
#include <driver/i2s_common.h>

#define TAG "AudioCodec"

AudioCodec::AudioCodec() {
}

AudioCodec::~AudioCodec() {
}

void AudioCodec::OutputData(std::vector<int16_t>& data) {
    Write(data.data(), data.size());
}

bool AudioCodec::InputData(std::vector<int16_t>& data) {
    int samples = Read(data.data(), data.size());
    if (samples > 0) {
        return true;
    }
    return false;
}

void AudioCodec::Start() {
    nvs_handle_t audio_settings;
    if (nvs_open("audio", NVS_READONLY, &audio_settings) == ESP_OK) {
        int32_t saved_volume = output_volume_;
        if (nvs_get_i32(audio_settings, "output_volume", &saved_volume) == ESP_OK) {
            output_volume_ = saved_volume;
        }
        nvs_close(audio_settings);
    }
    if (output_volume_ <= 0) {
        ESP_LOGW(TAG, "Output volume value (%d) is too small, setting to default (10)", output_volume_);
        output_volume_ = 10;
    }

    if (tx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    }

    if (rx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    }

    ESP_LOGI(TAG, "Audio codec started");
}
