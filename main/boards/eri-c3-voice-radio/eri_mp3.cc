#include "eri_mp3.h"

#include <esp_log.h>

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"

#define TAG "EriMp3"

Mp3Decoder::~Mp3Decoder() { Close(); }

bool Mp3Decoder::Open() {
    static bool registered = false;
    if (!registered) {
        auto r = esp_audio_dec_register_default();
        if (r != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "esp_audio_dec_register_default failed: %d", (int)r);
            return false;
        }
        r = esp_audio_simple_dec_register_default();
        if (r != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "esp_audio_simple_dec_register_default failed: %d", (int)r);
            return false;
        }
        registered = true;
    }

    esp_audio_simple_dec_cfg_t cfg = {};
    cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    esp_audio_simple_dec_handle_t h = nullptr;
    auto r = esp_audio_simple_dec_open(&cfg, &h);
    if (r != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "esp_audio_simple_dec_open failed: %d", (int)r);
        return false;
    }
    handle_ = h;
    return true;
}

void Mp3Decoder::Close() {
    if (handle_ != nullptr) {
        esp_audio_simple_dec_close(static_cast<esp_audio_simple_dec_handle_t>(handle_));
        handle_ = nullptr;
    }
}

bool Mp3Decoder::Reset() {
    Close();
    return Open();
}

bool Mp3Decoder::Decode(const uint8_t* in, int in_len, int* consumed, uint8_t* out, int out_cap,
                        int* out_bytes) {
    *consumed = 0;
    *out_bytes = 0;
    if (handle_ == nullptr) {
        return false;
    }

    esp_audio_simple_dec_raw_t raw = {};
    raw.buffer = const_cast<uint8_t*>(in);
    raw.len = static_cast<uint32_t>(in_len);
    raw.eos = false;

    esp_audio_simple_dec_out_t frame = {};
    frame.buffer = out;
    frame.len = static_cast<uint32_t>(out_cap);

    auto r = esp_audio_simple_dec_process(static_cast<esp_audio_simple_dec_handle_t>(handle_),
                                          &raw, &frame);
    *consumed = static_cast<int>(raw.consumed);
    *out_bytes = static_cast<int>(frame.decoded_size);

    if (r == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
        ESP_LOGW(TAG, "Output buffer too small, need %u", (unsigned)frame.needed_size);
        return false;
    }
    return r == ESP_AUDIO_ERR_OK;
}

bool Mp3Decoder::GetInfo(int* sample_rate, int* channels, int* bits) {
    if (handle_ == nullptr) {
        return false;
    }
    esp_audio_simple_dec_info_t info = {};
    auto r = esp_audio_simple_dec_get_info(static_cast<esp_audio_simple_dec_handle_t>(handle_),
                                           &info);
    if (r != ESP_AUDIO_ERR_OK) {
        return false;
    }
    *sample_rate = static_cast<int>(info.sample_rate);
    *channels = static_cast<int>(info.channel);
    *bits = static_cast<int>(info.bits_per_sample);
    return true;
}
