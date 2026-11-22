#ifndef _NO_AUDIO_CODEC_H
#define _NO_AUDIO_CODEC_H

#include "audio_codec.h"
#include <driver/gpio.h>
#include <mutex>

// Separate I2S0 speaker and I2S1 microphone, both mono left-slot, 32-bit words.
class NoAudioCodecSimplex : public AudioCodec {
public:
    NoAudioCodecSimplex(int input_sample_rate, int output_sample_rate,
        gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
        gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din);
    ~NoAudioCodecSimplex() override;
#if CONFIG_COMPANION_MICROPHONE_PREP
    esp_err_t ReadLocalCaptureRaw(int32_t* data, size_t capacity_samples,
                                size_t& samples_read, uint32_t timeout_ms) override;
#endif
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    esp_err_t WriteDiagnosticPcm(const int16_t* data, size_t samples,
                                size_t& written, uint32_t timeout_ms) override;
#endif
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
    esp_err_t ReadLocalCaptureInput(int16_t* data, size_t capacity_samples,
                                  size_t& samples_read, uint32_t timeout_ms) override;
#endif

private:
    std::mutex data_if_mutex_;
    int Write(const int16_t* data, int samples) override;
    int Read(int16_t* dest, int samples) override;
};

#endif
