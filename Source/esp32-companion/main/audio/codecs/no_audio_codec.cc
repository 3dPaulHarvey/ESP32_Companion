#include "no_audio_codec.h"

#include <esp_log.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
#include <memory>
#include <new>
#endif

#define TAG "NoAudioCodec"

namespace {
void ConvertInputPcm(const int32_t* input, int16_t* output, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        const int32_t value = input[i] >> 12;
        output[i] = value > INT16_MAX ? INT16_MAX : value < -INT16_MAX ? -INT16_MAX : static_cast<int16_t>(value);
    }
}
}  // namespace

NoAudioCodecSimplex::~NoAudioCodecSimplex() {
    if (rx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
    }
    if (tx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    }
}

NoAudioCodecSimplex::NoAudioCodecSimplex(int input_sample_rate, int output_sample_rate, gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout, gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din) {
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // Create a new channel for speaker
    i2s_chan_config_t chan_cfg = {
        .id = (i2s_port_t)0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,

        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false

        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk,
            .ws = spk_ws,
            .dout = spk_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

    // Create a new channel for MIC
    chan_cfg.id = (i2s_port_t)1;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_));
    std_cfg.clk_cfg.sample_rate_hz = (uint32_t)input_sample_rate_;
    std_cfg.gpio_cfg.bclk = mic_sck;
    std_cfg.gpio_cfg.ws = mic_ws;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = mic_din;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Simplex channels created");
}

int NoAudioCodecSimplex::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    std::vector<int32_t> buffer(samples);

    // output_volume_: 0-100
    // volume_factor_: 0-65536
    int32_t volume_factor = pow(double(output_volume_) / 100.0, 2) * 65536;
    for (int i = 0; i < samples; i++) {
        int64_t temp = int64_t(data[i]) * volume_factor; // 使用 int64_t 进行乘法运算
        if (temp > INT32_MAX) {
            buffer[i] = INT32_MAX;
        } else if (temp < INT32_MIN) {
            buffer[i] = INT32_MIN;
        } else {
            buffer[i] = static_cast<int32_t>(temp);
        }
    }

    size_t bytes_written;
    ESP_ERROR_CHECK(i2s_channel_write(tx_handle_, buffer.data(), samples * sizeof(int32_t), &bytes_written, portMAX_DELAY));
    return bytes_written / sizeof(int32_t);
}

#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
esp_err_t NoAudioCodecSimplex::WriteDiagnosticPcm(const int16_t* data, size_t samples,
                                          size_t& written, uint32_t timeout_ms) {
    written = 0;
    if (!data || !samples || samples > 480 || timeout_ms > 250) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    std::unique_ptr<int32_t[]> buffer(new (std::nothrow) int32_t[samples]);
    if (!buffer) return ESP_ERR_NO_MEM;
    const int32_t factor = pow(double(output_volume_) / 100.0, 2) * 65536;
    for (size_t i = 0; i < samples; ++i) {
        // Same 16-to-32-bit slot conversion and saved volume as normal output,
        // with half amplitude for this first speech test.
        const int64_t value = int64_t(data[i]) * factor / 2;
        buffer[i] = value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : static_cast<int32_t>(value);
    }
    size_t bytes = 0;
    const auto error = i2s_channel_write(tx_handle_, buffer.get(), samples * sizeof(int32_t), &bytes, timeout_ms);
    written = std::min(bytes / sizeof(int32_t), samples);
    return bytes % sizeof(int32_t) ? ESP_FAIL : error;
}
#endif

int NoAudioCodecSimplex::Read(int16_t* dest, int samples) {
    size_t bytes_read;

    std::vector<int32_t> bit32_buffer(samples);
    if (i2s_channel_read(rx_handle_, bit32_buffer.data(), samples * sizeof(int32_t), &bytes_read, portMAX_DELAY) != ESP_OK) {
        ESP_LOGE(TAG, "Read Failed!");
        return 0;
    }

    samples = bytes_read / sizeof(int32_t);
    ConvertInputPcm(bit32_buffer.data(), dest, samples);
    return samples;
}

#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
esp_err_t NoAudioCodecSimplex::ReadLocalCaptureInput(int16_t* dest, size_t capacity_samples,
                                            size_t& samples_read, uint32_t timeout_ms) {
    samples_read = 0;
    // Keep this diagnostic operation small and bounded, including its temporary.
    if (dest == nullptr || capacity_samples == 0 || capacity_samples > 320 || timeout_ms > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    std::unique_ptr<int32_t[]> raw(new (std::nothrow) int32_t[capacity_samples]);
    if (!raw) {
        return ESP_ERR_NO_MEM;
    }
    size_t bytes_read = 0;
    const esp_err_t result = i2s_channel_read(rx_handle_, raw.get(), capacity_samples * sizeof(int32_t),
                                             &bytes_read, timeout_ms);
    if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
        return result;
    }
    // A timeout can contain real partial data; report only complete I2S words.
    samples_read = std::min(bytes_read / sizeof(int32_t), capacity_samples);
    ConvertInputPcm(raw.get(), dest, samples_read);
    return result;
}
#endif

#if CONFIG_COMPANION_MICROPHONE_PREP
esp_err_t NoAudioCodecSimplex::ReadLocalCaptureRaw(int32_t* dest, size_t capacity_samples,
                                          size_t& samples_read, uint32_t timeout_ms) {
    samples_read = 0;
    if (!dest || !capacity_samples || capacity_samples > 320 || timeout_ms > 100) return ESP_ERR_INVALID_ARG;
    size_t bytes = 0;
    const auto error = i2s_channel_read(rx_handle_, dest, capacity_samples * sizeof(int32_t), &bytes, timeout_ms);
    samples_read = std::min(bytes / sizeof(int32_t), capacity_samples);
    return bytes % sizeof(int32_t) ? ESP_FAIL : error;
}
#endif
