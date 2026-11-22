#ifndef _AUDIO_CODEC_H
#define _AUDIO_CODEC_H

#include <freertos/FreeRTOS.h>
#include <driver/i2s_std.h>

#include <vector>


#define AUDIO_CODEC_DMA_DESC_NUM 6
#define AUDIO_CODEC_DMA_FRAME_NUM 240

class AudioCodec {
public:
    AudioCodec();
    virtual ~AudioCodec();
    

    virtual void OutputData(std::vector<int16_t>& data);
    virtual bool InputData(std::vector<int16_t>& data);
#if CONFIG_COMPANION_GEMINI_VOICE_PROBE
    virtual esp_err_t WriteDiagnosticPcm(const int16_t* data, size_t samples,
                                        size_t& written, uint32_t timeout_ms) = 0;
#endif
#if CONFIG_COMPANION_HARDWARE_DIAGNOSTICS
    // Exact-count, finite-wait path for local microphone capture.
    virtual esp_err_t ReadLocalCaptureInput(int16_t* data, size_t capacity_samples,
                                          size_t& samples_read, uint32_t timeout_ms) = 0;
#endif
#if CONFIG_COMPANION_MICROPHONE_PREP
    virtual esp_err_t ReadLocalCaptureRaw(int32_t* data, size_t capacity_samples,
                                        size_t& samples_read, uint32_t timeout_ms) = 0;
#endif
    virtual void Start();

    inline bool input_reference() const { return false; }
    inline int input_sample_rate() const { return input_sample_rate_; }
    inline int output_sample_rate() const { return output_sample_rate_; }
    inline int input_channels() const { return 1; }
    inline int output_channels() const { return 1; }
    inline int output_volume() const { return output_volume_; }

protected:
    i2s_chan_handle_t tx_handle_ = nullptr;
    i2s_chan_handle_t rx_handle_ = nullptr;

    int input_sample_rate_ = 0;
    int output_sample_rate_ = 0;
    int output_volume_ = 70;

    virtual int Read(int16_t* dest, int samples) = 0;
    virtual int Write(const int16_t* data, int samples) = 0;
};

#endif // _AUDIO_CODEC_H
