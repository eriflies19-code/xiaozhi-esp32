#include "radio_mode.h"

#include "eri_c3_voice_radio_board.h"

#include "audio_codec.h"
#include "display.h"
#include <decoder/esp_audio_dec_default.h>
#include <esp_audio_simple_dec.h>
#include <esp_ae_rate_cvt.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <freertos/stream_buffer.h>
#include <simple_dec/esp_audio_simple_dec_default.h>
#include <wifi_manager.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <vector>

#define TAG "InternetRadio"

namespace {

constexpr size_t kStreamBufferSize = 22528;
constexpr size_t kStreamTriggerSize = 1024;
constexpr size_t kHttpReadSize = 1024;
constexpr int kOutputSampleRate = 16000;

struct Station {
    const char* name;
    const char* url;
};

constexpr std::array<Station, 5> kStations{{
    {"Vietnam Nation", "http://c13.radioboss.fm:8127/stream"},
    {"Viet Radio 1560", "http://71.4.56.20/3rdout/radio.hou.std2/icecast.audio"},
    {"Music Lake", "http://nap.casthost.net:8626/stream"},
    {"Groove Salad", "https://ice2.somafm.com/groovesalad-128-mp3"},
    {"Radio Paradise", "http://stream-dc1.radioparadise.com/mp3-32"},
}};

StreamBufferHandle_t stream_buffer = nullptr;
std::atomic<uint32_t> station_generation{0};
std::atomic<bool> stop_radio{false};
std::atomic<bool> http_task_done{false};
std::atomic<bool> decoder_task_done{false};

void HttpStreamTask(void* arg) {
    auto* board = static_cast<EriC3VoiceRadioBoard*>(arg);
    std::array<uint8_t, kHttpReadSize> input{};
    uint8_t active_station = 0xff;
    uint32_t active_generation = 0;

    while (!stop_radio.load()) {
        const uint8_t selected = board->GetRadioStation();
        const uint32_t generation = station_generation.load();
        if (active_station != selected || active_generation != generation) {
            if (active_station != 0xff) {
                xStreamBufferReset(stream_buffer);
            }
            active_station = selected;
            active_generation = generation;

            esp_http_client_config_t config = {};
            config.url = kStations[active_station].url;
            config.timeout_ms = 10000;
            config.buffer_size = 2048;
            config.crt_bundle_attach = esp_crt_bundle_attach;
            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client == nullptr) {
                ESP_LOGE(TAG, "Failed to create HTTP client");
                active_station = 0xff;
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }

            esp_err_t err = esp_http_client_open(client, 0);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Could not open stream %s: %s", kStations[active_station].name,
                         esp_err_to_name(err));
                esp_http_client_cleanup(client);
                active_station = 0xff;
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            const int64_t content_length = esp_http_client_fetch_headers(client);
            if (content_length < 0) {
                ESP_LOGW(TAG, "Failed to fetch radio response headers");
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                active_station = 0xff;
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            const int status_code = esp_http_client_get_status_code(client);
            if (status_code < 200 || status_code >= 300) {
                ESP_LOGW(TAG, "Radio server returned HTTP %d", status_code);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                active_station = 0xff;
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            ESP_LOGI(TAG, "Streaming %s", kStations[active_station].name);
            TickType_t last_data_time = xTaskGetTickCount();

            while (!stop_radio.load() &&
                   station_generation.load() == active_generation) {
                const int bytes_read = esp_http_client_read(
                    client, reinterpret_cast<char*>(input.data()), input.size());
                if (bytes_read < 0) {
                    ESP_LOGW(TAG, "HTTP stream read failed");
                    break;
                }
                if (bytes_read == 0) {
                    if (xTaskGetTickCount() - last_data_time > pdMS_TO_TICKS(15000)) {
                        ESP_LOGW(TAG, "Radio stream timed out");
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
                last_data_time = xTaskGetTickCount();
                size_t sent = 0;
                while (sent < static_cast<size_t>(bytes_read) &&
                       !stop_radio.load() &&
                       station_generation.load() == active_generation) {
                    sent += xStreamBufferSend(
                        stream_buffer, input.data() + sent,
                        static_cast<size_t>(bytes_read) - sent, pdMS_TO_TICKS(500));
                }
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            if (!stop_radio.load() && station_generation.load() == active_generation) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            active_station = 0xff;
        }
    }
    http_task_done.store(true);
    vTaskDelete(nullptr);
}

bool ConvertAndPlay(AudioCodec* codec, esp_ae_rate_cvt_handle_t* resampler,
                    const int16_t* pcm, uint32_t pcm_samples, uint8_t channels,
                    uint32_t sample_rate, std::vector<int16_t>& mono,
                    std::vector<int16_t>& output) {
    if (*resampler == nullptr) {
        esp_ae_rate_cvt_cfg_t cfg = {
            .src_rate = sample_rate,
            .dest_rate = kOutputSampleRate,
            .channel = 1,
            .bits_per_sample = ESP_AUDIO_BIT16,
            .complexity = 1,
            .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY,
        };
        if (esp_ae_rate_cvt_open(&cfg, resampler) != ESP_AE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create radio resampler");
            return false;
        }
    }

    const uint32_t mono_samples = channels == 2 ? pcm_samples / 2 : pcm_samples;
    mono.resize(mono_samples);
    if (channels == 2) {
        for (uint32_t i = 0; i < mono_samples; ++i) {
            mono[i] = pcm[i * 2];
        }
    } else {
        std::copy_n(pcm, mono_samples, mono.data());
    }

    uint32_t output_capacity = 0;
    if (esp_ae_rate_cvt_get_max_out_sample_num(*resampler, mono_samples,
                                               &output_capacity) != ESP_AE_ERR_OK) {
        ESP_LOGE(TAG, "Failed to size resampled radio frame");
        return false;
    }
    output.resize(output_capacity);
    uint32_t output_samples = output_capacity;
    if (esp_ae_rate_cvt_process(*resampler, mono.data(), mono_samples, output.data(),
                                &output_samples) != ESP_AE_ERR_OK) {
        ESP_LOGE(TAG, "Failed to resample radio frame");
        return false;
    }
    output.resize(output_samples);
    if (!codec->output_enabled()) {
        codec->EnableOutput(true);
    }
    codec->OutputData(output);
    return true;
}

void Mp3PlaybackTask(void* arg) {
    auto* board = static_cast<EriC3VoiceRadioBoard*>(arg);
    auto* codec = board->GetAudioCodec();

    if (esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Failed to register audio decoders");
        stop_radio.store(true);
        decoder_task_done.store(true);
        vTaskDelete(nullptr);
        return;
    }
    if (esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Failed to register simple audio decoders");
        esp_audio_dec_unregister_default();
        stop_radio.store(true);
        decoder_task_done.store(true);
        vTaskDelete(nullptr);
        return;
    }

    esp_audio_simple_dec_cfg_t decoder_config = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg = nullptr,
        .cfg_size = 0,
        .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t decoder = nullptr;
    if (esp_audio_simple_dec_open(&decoder_config, &decoder) != ESP_AUDIO_ERR_OK ||
        decoder == nullptr) {
        ESP_LOGE(TAG, "Failed to open MP3 decoder");
        stop_radio.store(true);
        esp_audio_simple_dec_unregister_default();
        esp_audio_dec_unregister_default();
        decoder_task_done.store(true);
        vTaskDelete(nullptr);
        return;
    }

    std::array<uint8_t, kHttpReadSize> encoded{};
    std::vector<int16_t> decoded(4608);
    std::vector<int16_t> mono;
    std::vector<int16_t> output;
    esp_ae_rate_cvt_handle_t resampler = nullptr;
    uint32_t active_generation = station_generation.load();

    while (!stop_radio.load()) {
        const uint32_t generation = station_generation.load();
        if (generation != active_generation) {
            active_generation = generation;
            esp_audio_simple_dec_reset(decoder);
            if (resampler != nullptr) {
                esp_ae_rate_cvt_close(resampler);
                resampler = nullptr;
            }
        }
        const size_t received = xStreamBufferReceive(
            stream_buffer, encoded.data(), encoded.size(), pdMS_TO_TICKS(500));
        if (received == 0) {
            continue;
        }
        esp_audio_simple_dec_raw_t raw = {
            .buffer = encoded.data(),
            .len = static_cast<uint32_t>(received),
            .eos = false,
            .consumed = 0,
            .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
        };
        do {
            const uint32_t consumed_before = raw.consumed;
            esp_audio_simple_dec_out_t frame = {
                .buffer = reinterpret_cast<uint8_t*>(decoded.data()),
                .len = static_cast<uint32_t>(decoded.size() * sizeof(int16_t)),
                .needed_size = 0,
                .decoded_size = 0,
            };
            const auto result = esp_audio_simple_dec_process(decoder, &raw, &frame);
            if (result != ESP_AUDIO_ERR_OK && result != ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                ESP_LOGW(TAG, "MP3 decode returned %d", result);
                break;
            }
            if (frame.decoded_size > 0) {
                esp_audio_simple_dec_info_t info = {};
                if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(TAG, "Failed to get MP3 decoder output format");
                    stop_radio.store(true);
                    break;
                }
                if (!ConvertAndPlay(codec, &resampler, decoded.data(),
                                    frame.decoded_size / sizeof(int16_t), info.channel,
                                    info.sample_rate, mono, output)) {
                    stop_radio.store(true);
                    break;
                }
            }
            if (raw.consumed == consumed_before && frame.decoded_size == 0) {
                break;
            }
        } while (raw.consumed < raw.len && !stop_radio.load());
    }

    if (resampler != nullptr) {
        esp_ae_rate_cvt_close(resampler);
    }
    esp_audio_simple_dec_close(decoder);
    esp_audio_simple_dec_unregister_default();
    esp_audio_dec_unregister_default();
    decoder_task_done.store(true);
    vTaskDelete(nullptr);
}

}  // namespace

void RunInternetRadioMode(EriC3VoiceRadioBoard& board) {
    auto* display = board.GetDisplay();
    board.GetAudioCodec()->Start();
    display->SetStatus("Internet Radio");
    display->SetChatMessage("system", "Connecting to Wi-Fi...");
    board.StartNetwork();

    auto& wifi = WifiManager::GetInstance();
    while (!wifi.IsConnected()) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    display->ClearChatMessages();
    display->SetChatMessage("system", "GPIO4: next station");
    display->ShowNotification("Radio ready");

    stream_buffer = xStreamBufferCreate(kStreamBufferSize, kStreamTriggerSize);
    if (stream_buffer == nullptr) {
        ESP_LOGE(TAG, "Unable to allocate radio stream buffer");
        display->ShowNotification("Radio memory allocation failed", 10000);
        return;
    }

    stop_radio.store(false);
    station_generation.store(0);
    http_task_done.store(false);
    decoder_task_done.store(false);
    ESP_LOGI(TAG, "Radio mode heap: free=%u, largest=%u",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (xTaskCreate(HttpStreamTask, "radio_http", 4096, &board, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create radio HTTP task");
        display->ShowNotification("Radio HTTP task failed", 10000);
        vStreamBufferDelete(stream_buffer);
        stream_buffer = nullptr;
        return;
    }
    if (xTaskCreate(Mp3PlaybackTask, "radio_mp3", 4096, &board, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create radio decoder task");
        stop_radio.store(true);
        decoder_task_done.store(true);
        display->ShowNotification("Radio decoder task failed", 10000);
    } else {
        uint8_t last_station = board.GetRadioStation();
        while (!stop_radio.load()) {
            const uint8_t selected = board.GetRadioStation();
            if (selected != last_station) {
                last_station = selected;
                station_generation.fetch_add(1);
                display->SetChatMessage("system", kStations[selected].name);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    while (!http_task_done.load() || !decoder_task_done.load()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vStreamBufferDelete(stream_buffer);
    stream_buffer = nullptr;
}
