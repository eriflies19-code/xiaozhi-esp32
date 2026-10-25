#include "eri_screen.h"
#include "eri_gfx.h"

#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string.h>

#define TAG "EriScreen"

// Tay cam OLED do board tao (eri_c3_voice_board.cc)
extern esp_lcd_panel_handle_t g_eri_oled_panel;

namespace EriScreen {

static uint8_t g_sent[EriGfx::kBufBytes];
static bool g_started = false;

void Begin() {
    if (g_started) {
        return;
    }
    g_started = true;
    lvgl_port_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    memset(g_sent, 0xFF, sizeof(g_sent));  // ep ve khung dau tien
    if (g_eri_oled_panel == nullptr) {
        ESP_LOGE(TAG, "Khong co tay cam OLED");
    }
}

void Present() {
    if (g_eri_oled_panel == nullptr) {
        return;
    }
    const uint8_t* fb = EriGfx::Buffer();
    if (memcmp(fb, g_sent, sizeof(g_sent)) == 0) {
        return;  // khong doi: khong ton bang thong I2C
    }
    memcpy(g_sent, fb, sizeof(g_sent));
    esp_lcd_panel_draw_bitmap(g_eri_oled_panel, 0, 0, EriGfx::kW, EriGfx::kH, fb);
}

}  // namespace EriScreen
