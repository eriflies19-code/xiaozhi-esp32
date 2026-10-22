#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#if CONFIG_BOARD_TYPE_ERI_C3_VOICE_RADIO
#include "board.h"
#include "boards/eri-c3-voice-radio/eri_c3_voice_radio_board.h"
#include "boards/eri-c3-voice-radio/radio_mode.h"
#endif

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if CONFIG_BOARD_TYPE_ERI_C3_VOICE_RADIO
    auto& board = static_cast<EriC3VoiceRadioBoard&>(Board::GetInstance());
    if (board.WaitForBootMode() == EriC3VoiceRadioBoard::BootMode::Radio) {
        board.StartRadioMode();
        RunInternetRadioMode(board);
        return;
    }
#endif

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}
