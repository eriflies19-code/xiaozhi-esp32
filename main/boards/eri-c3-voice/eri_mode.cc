#include "eri_mode.h"
#include "eri_gfx.h"
#include "eri_hud.h"
#include "eri_screen.h"
#include "eri_ui.h"
#include "eri_radio.h"

#include "board.h"
#include "display.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>

#define TAG "EriMode"

namespace EriMode {

static constexpr int kMenuSeconds = 5;
static constexpr int kModeCount = 3;  // 0 = Chat AI, 1 = Radio, 2 = WAZE HUD

static std::atomic<int> g_mode{static_cast<int>(Mode::kChat)};
static std::atomic<int> g_sel{0};          // 0 = Chat AI, 1 = Radio, 2 = WAZE HUD
static std::atomic<bool> g_touched{false}; // da bam nut -> tat dem nguoc
static std::atomic<bool> g_confirm{false};
static std::atomic<uint32_t> g_menu_start_ms{0};  // bo qua nut bam luc moi vao menu

static uint32_t NowMs() { return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS); }
static bool MenuSettling() { return NowMs() - g_menu_start_ms.load() < 800; }

bool Run() {
    {
        Settings settings("eri", true);
        if (settings.GetInt("skip", 0) != 0) {
            settings.SetInt("skip", 0);  // vua khoi dong lai de vao Chat AI: bo qua menu
            g_mode = static_cast<int>(Mode::kChat);
            return false;
        }
    }

    Board::GetInstance();  // tao board (man hinh, nut bam) truoc khi ve menu

    int last = 0;
    bool stay = false;  // nguoi dung chu dong ve menu: cho chon, khong tu vao lai
    {
        Settings settings("eri", true);
        last = settings.GetInt("mode", 0);
        stay = settings.GetInt("stay", 0) != 0;
        if (stay) {
            settings.SetInt("stay", 0);
        }
    }
    g_sel = (last >= 0 && last < kModeCount) ? last : 0;
    g_touched = false;
    g_confirm = false;
    g_menu_start_ms = NowMs();
    g_mode = static_cast<int>(Mode::kMenu);
    ESP_LOGI(TAG, "Menu started, last mode=%d", last);

    EriScreen::Begin();  // dung LVGL, menu ve bang EriGfx

    const int total_ms = kMenuSeconds * 1000;
    uint32_t t0 = NowMs();
    bool counting = !stay;
    while (!g_confirm) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (g_touched) {
            counting = false;  // da bam nut: cho xac nhan, khong tu vao nua
        }
        int left = 0;
        if (counting) {
            left = total_ms - static_cast<int>(NowMs() - t0);
            if (left <= 0) {
                break;  // het gio: dung che do lan truoc
            }
        }
        EriUi::DrawMenu(g_sel, left, counting ? total_ms : 0);
        EriScreen::Present();
    }

    int sel = g_sel;
    {
        Settings settings("eri", true);
        settings.SetInt("mode", sel);
        if (sel == 0) {
            settings.SetInt("skip", 1);
        }
    }
    ESP_LOGI(TAG, "Selected mode: %s", sel == 2 ? "waze" : (sel == 1 ? "radio" : "chat"));

    if (sel == 1) {
        g_mode = static_cast<int>(Mode::kRadio);
        EriRadio::Start();
        return true;
    }
    if (sel == 2) {
        g_mode = static_cast<int>(Mode::kHud);
        EriHud::Start();
        return true;
    }

    // Chat AI can LVGL nguyen ven: khoi dong lai de LVGL chay tu dau (lan sau bo qua menu)
    EriGfx::Clear();
    EriGfx::SetColor(true);
    EriGfx::TextC(EriGfx::kFontM, 64, 36, "Starting Chat AI...");
    EriScreen::Present();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return false;
}

Mode Current() { return static_cast<Mode>(g_mode.load()); }

bool HandleChatClick() {
    ESP_LOGI(TAG, "HandleChatClick mode=%d", (int)Current());
    switch (Current()) {
        case Mode::kMenu:
            if (MenuSettling()) {
                return true;  // bo qua: co the la nhac nut vua giu de ve menu
            }
            g_touched = true;
            g_confirm = true;
            return true;
        case Mode::kRadio:
            EriRadio::NextStation();
            return true;
        case Mode::kHud:
            EriHud::ToggleMute();
            return true;
        default:
            return false;  // Chat AI: de board xu ly nhu cu
    }
}

bool HandleChatLongPress() {
    ESP_LOGI(TAG, "HandleChatLongPress mode=%d", (int)Current());
    if (Current() == Mode::kMenu) {
        return true;
    }
    RestartToMenu();
    return true;
}

bool HandleVolumeClick() {
    ESP_LOGI(TAG, "HandleVolumeClick mode=%d", (int)Current());
    switch (Current()) {
        case Mode::kMenu:
            if (MenuSettling()) {
                return true;
            }
            g_touched = true;
            g_sel = (g_sel.load() + 1) % kModeCount;
            return true;
        case Mode::kRadio:
            EriRadio::VolumeStep(+10);
            return true;
        case Mode::kHud:
            EriHud::ToggleDemo();
            return true;
        default:
            return false;
    }
}

bool HandleVolumeLongPress() {
    ESP_LOGI(TAG, "HandleVolumeLongPress mode=%d", (int)Current());
    switch (Current()) {
        case Mode::kMenu:
            return true;
        case Mode::kRadio:
            EriRadio::VolumeStep(-10);
            return true;
        case Mode::kHud:
            return true;
        default:
            return false;
    }
}

void RestartToMenu() {
    ESP_LOGI(TAG, "Restarting to show mode menu");
    {
        Settings settings("eri", true);
        settings.SetInt("stay", 1);
    }
    if (Current() == Mode::kChat) {
        Board::GetInstance().GetDisplay()->ShowNotification("Back to menu...", 1500);
    } else {
        EriGfx::Clear();
        EriGfx::SetColor(true);
        EriGfx::TextC(EriGfx::kFontM, 64, 36, "Back to menu...");
        EriScreen::Present();
    }
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
}

}  // namespace EriMode
