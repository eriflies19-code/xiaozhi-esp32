#pragma once

// Tu tat nguon (deep sleep) khi khong thao tac hoac pin can. Bam nut GPIO4 de danh thuc (may khoi dong
// lai nhu cap nguon: ve menu roi tu vao che do lan truoc).
// Luu y: deep sleep khong cat nguon cho loa/mic/man hinh roi rac, nen van ton vai mA. Muon tat han
// hay dung cong tac nguon.
//
// Header-only de khong phai touch CMake.

#include "config.h"
#include "eri_battery.h"
#include "eri_screen.h"
#include "eri_ui.h"

#include <driver/gpio.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern esp_lcd_panel_handle_t g_eri_oled_panel;  // do board tao

namespace EriPower {

inline constexpr gpio_num_t kWakeGpio = CHAT_BUTTON_GPIO;  // GPIO4, bam xuong GND
inline bool g_sleeping = false;

// Ten ham dat nguon danh thuc GPIO tu deep sleep doi theo phien ban ESP-IDF: thu ten moi truoc,
// khong co thi dung ten cu (SFINAE: chi 1 trong 2 duoc bien dich)
namespace detail {
template <class M>
auto EnableGpioWake(M mask, int)
    -> decltype(esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(mask, ESP_GPIO_WAKEUP_GPIO_LOW), void()) {
    esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(mask, ESP_GPIO_WAKEUP_GPIO_LOW);
}
template <class M>
auto EnableGpioWake(M mask, long) -> decltype(esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW), void()) {
    esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW);
}
}  // namespace detail

// Goi dau constructor cua board: sau khi thuc day tu deep sleep, chan con bi giu nen phai nha ra
inline void ReleaseHold() {
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(kWakeGpio);
}

// Hien thong bao ngan roi vao deep sleep. Khong tro ve.
inline void Sleep(const char* line1, const char* line2) {
    if (g_sleeping) {
        vTaskDelay(portMAX_DELAY);
    }
    g_sleeping = true;
    ESP_LOGW("EriPower", "Deep sleep: %s", line1);
    EriScreen::Begin();  // dung LVGL de ve bang EriGfx
    EriUi::DrawNotice(line1, line2);
    EriScreen::Present();
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (g_eri_oled_panel != nullptr) {
        esp_lcd_panel_disp_on_off(g_eri_oled_panel, false);
    }
    esp_wifi_stop();
    gpio_pullup_en(kWakeGpio);
    gpio_pulldown_dis(kWakeGpio);
    gpio_hold_en(kWakeGpio);
    gpio_deep_sleep_hold_en();
    detail::EnableGpioWake(static_cast<uint64_t>(1ULL << kWakeGpio), 0);
    esp_deep_sleep_start();
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}

// true neu da qua `limit_ms` ke tu lan thao tac cuoi `last_act_ms` (0 = khong gioi han).
// last_act_ms la moc esp_timer (ms) giong cac app dang dung (NowMs). So sanh co dau de moc o phia truoc
// nhin chenh vai ms khong bi hieu nham thanh "da lau lam roi".
inline bool IdleFor(uint32_t last_act_ms, uint32_t limit_ms) {
    if (limit_ms == 0) {
        return false;
    }
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    int32_t idle = static_cast<int32_t>(now - last_act_ms);
    return idle > 0 && static_cast<uint32_t>(idle) > limit_ms;
}

// Canh pin: pin can thi tu tat de bao ve pin
inline void WatchTask(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (EriBattery::Critical()) {
            Sleep("Battery empty", "Charge me");
        }
    }
}

inline void Start() {
    xTaskCreate(WatchTask, "eri_power", 3072, nullptr, 2, nullptr);
}

}  // namespace EriPower
