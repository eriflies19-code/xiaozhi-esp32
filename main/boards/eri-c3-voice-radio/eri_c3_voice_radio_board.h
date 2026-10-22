#ifndef ERI_C3_VOICE_RADIO_BOARD_H
#define ERI_C3_VOICE_RADIO_BOARD_H

#include "wifi_board.h"
#include "button.h"

#include <atomic>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>

class EriC3VoiceRadioBoard : public WifiBoard {
public:
    enum class BootMode {
        Xiaozhi,
        Radio,
    };

    EriC3VoiceRadioBoard();
    BootMode WaitForBootMode();
    void StartRadioMode();
    void SelectNextRadioStation();
    void IncreaseRadioVolume();
    uint8_t GetRadioStation() const { return radio_station_.load(); }

    AudioCodec* GetAudioCodec() override;
    Display* GetDisplay() override;

private:
    void InitializeDisplayI2c();
    void InitializeSsd1306Display();
    void InitializeButtons();

    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    Button chat_button_;
    Button volume_button_;
    Display* display_ = nullptr;
    std::atomic<int> selected_boot_mode_{-1};
    std::atomic<bool> boot_menu_active_{false};
    std::atomic<bool> radio_mode_active_{false};
    std::atomic<uint8_t> radio_station_{0};
};

#endif
