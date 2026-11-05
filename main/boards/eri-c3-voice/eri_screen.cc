#include "eri_screen.h"
#include "eri_battery.h"
#include "eri_gfx.h"
#include "eri_ui.h"
#include "settings.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string.h>

#define TAG "EriScreen"

// Tay cam OLED do board tao (eri_c3_voice_board.cc)
extern esp_lcd_panel_handle_t g_eri_oled_panel;
extern esp_lcd_panel_io_handle_t g_eri_oled_io;

namespace EriScreen {

// Do tuong phan OLED 0..255 (mac dinh cua chip ~0x7F). Thap hon = do sang thap hon, tiet kiem dien.
static const uint8_t kContrast[kBrightLevels] = {0x08, 0x28, 0x50, 0x90, 0xFF};
static int g_level = -1;

int BrightLevel() {
    if (g_level < 0) {
        Settings st("eri", false);
        int l = st.GetInt("bright", 2);
        g_level = (l >= 0 && l < kBrightLevels) ? l : 2;
    }
    return g_level;
}

void ApplyBrightness() {
    if (g_eri_oled_io != nullptr) {
        uint8_t c = kContrast[BrightLevel()];
        esp_lcd_panel_io_tx_param(g_eri_oled_io, 0x81, &c, 1);  // SSD1306: lenh dat do tuong phan
    }
}

void SetBrightLevel(int l) {
    g_level = (l < 0) ? 0 : (l >= kBrightLevels ? kBrightLevels - 1 : l);
    ApplyBrightness();
}

void SaveBrightLevel() {
    Settings st("eri", true);
    st.SetInt("bright", BrightLevel());
}

static uint8_t g_sent[EriGfx::kBufBytes];
static bool g_started = false;

void Begin() {
    if (g_started) {
        return;
    }
    g_started = true;
    lvgl_port_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    ApplyBrightness();
    memset(g_sent, 0xFF, sizeof(g_sent));  // ep ve khung dau tien
    if (g_eri_oled_panel == nullptr) {
        ESP_LOGE(TAG, "Khong co tay cam OLED");
    }
}

void Present() {
    if (g_eri_oled_panel == nullptr) {
        return;
    }
    // Pin yeu: ve thong bao de len man hinh hien tai (cac app ve lai ca khung moi lan nen tu bien mat)
    if (EriBattery::BannerActive()) {
        EriUi::DrawLowBatteryBanner(EriBattery::Percent());
    }
    const uint8_t* fb = EriGfx::Buffer();
    if (memcmp(fb, g_sent, sizeof(g_sent)) == 0) {
        return;  // khong doi: khong ton bang thong I2C
    }
    memcpy(g_sent, fb, sizeof(g_sent));
    esp_lcd_panel_draw_bitmap(g_eri_oled_panel, 0, 0, EriGfx::kW, EriGfx::kH, fb);
}

}  // namespace EriScreen
