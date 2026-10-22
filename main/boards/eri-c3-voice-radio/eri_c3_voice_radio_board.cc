#include "eri_c3_voice_radio_board.h"

#include "application.h"
#include "audio_codec.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/oled_display.h"

#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <algorithm>
#include <string>

#define TAG "EriC3VoiceRadio"

void EriC3VoiceRadioBoard::InitializeDisplayI2c() {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = DISPLAY_SDA_PIN;
    bus_cfg.scl_io_num = DISPLAY_SCL_PIN;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = 1;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &display_i2c_bus_));
}

void EriC3VoiceRadioBoard::InitializeSsd1306Display() {
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = DISPLAY_I2C_ADDR;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 6;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.scl_speed_hz = 400 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io));

    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.bits_per_pixel = 1;

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {};
    ssd1306_config.height = DISPLAY_HEIGHT;
    panel_config.vendor_config = &ssd1306_config;

    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    display_ = new OledDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                               DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
}

void EriC3VoiceRadioBoard::InitializeButtons() {
    chat_button_.OnClick([this]() {
        if (boot_menu_active_.load()) {
            selected_boot_mode_.store(static_cast<int>(BootMode::Xiaozhi));
            return;
        }
        if (radio_mode_active_.load()) {
            SelectNextRadioStation();
            display_->ShowNotification("Next station");
            return;
        }
        Application::GetInstance().ToggleChatState();
    });

    volume_button_.OnClick([this]() {
        if (boot_menu_active_.load()) {
            selected_boot_mode_.store(static_cast<int>(BootMode::Radio));
            return;
        }
        if (radio_mode_active_.load()) {
            IncreaseRadioVolume();
            return;
        }
        auto codec = GetAudioCodec();
        constexpr int kVolumeStep = 10;
        constexpr int kMinimumVolume = 10;
        constexpr int kMaximumVolume = 100;
        int volume = codec->output_volume() >= kMaximumVolume
                         ? kMinimumVolume
                         : std::min(codec->output_volume() + kVolumeStep, kMaximumVolume);
        codec->SetOutputVolume(volume);
        GetDisplay()->ShowNotification("Volume " + std::to_string(volume));
    });
}

EriC3VoiceRadioBoard::EriC3VoiceRadioBoard()
    : chat_button_(CHAT_BUTTON_GPIO), volume_button_(VOLUME_BUTTON_GPIO) {
    InitializeDisplayI2c();
    InitializeSsd1306Display();
    InitializeButtons();
}

EriC3VoiceRadioBoard::BootMode EriC3VoiceRadioBoard::WaitForBootMode() {
    display_->SetupUI();
    display_->SetStatus("Choose mode");
    display_->SetChatMessage("system", "GPIO4: XiaoZhi  GPIO10: Radio");
    display_->ShowNotification("Default: XiaoZhi (10s)");
    ESP_LOGI(TAG, "Boot menu: click GPIO4 for XiaoZhi, GPIO10 for Radio");

    boot_menu_active_.store(true);
    const TickType_t timeout = pdMS_TO_TICKS(10000);
    const TickType_t poll_interval = pdMS_TO_TICKS(50);
    TickType_t elapsed = 0;
    while (elapsed < timeout && selected_boot_mode_.load() < 0) {
        vTaskDelay(poll_interval);
        elapsed += poll_interval;
    }
    boot_menu_active_.store(false);

    const int selected = selected_boot_mode_.load();
    const auto mode = selected == static_cast<int>(BootMode::Radio) ? BootMode::Radio
                                                                   : BootMode::Xiaozhi;
    display_->ClearChatMessages();
    ESP_LOGI(TAG, "Selected boot mode: %s", mode == BootMode::Radio ? "radio" : "xiaozhi");
    return mode;
}

void EriC3VoiceRadioBoard::StartRadioMode() {
    radio_mode_active_.store(true);
}

void EriC3VoiceRadioBoard::SelectNextRadioStation() {
    radio_station_.store((radio_station_.load() + 1) % 5);
}

void EriC3VoiceRadioBoard::IncreaseRadioVolume() {
    auto codec = GetAudioCodec();
    constexpr int kVolumeStep = 10;
    constexpr int kMinimumVolume = 10;
    constexpr int kMaximumVolume = 100;
    int volume = codec->output_volume() >= kMaximumVolume
                     ? kMinimumVolume
                     : std::min(codec->output_volume() + kVolumeStep, kMaximumVolume);
    codec->SetOutputVolume(volume);
    display_->ShowNotification("Volume " + std::to_string(volume));
}

AudioCodec* EriC3VoiceRadioBoard::GetAudioCodec() {
    static NoAudioCodecDuplex audio_codec(
        AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
        AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
    return &audio_codec;
}

Display* EriC3VoiceRadioBoard::GetDisplay() {
    return display_;
}

DECLARE_BOARD(EriC3VoiceRadioBoard);
