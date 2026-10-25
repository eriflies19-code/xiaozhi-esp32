#include "eri_mode.h"
#include "eri_hud.h"
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

static void ShowMenu(int sel, int secs_left) {
    auto display = Board::GetInstance().GetDisplay();
    static const char* const kTitles[kModeCount] = {
        "[AI] radio waze",
        "ai [RADIO] waze",
        "ai radio [WAZE]",
    };
    display->SetStatus(kTitles[sel % kModeCount]);
    char msg[64];
    if (secs_left >= 0) {
        snprintf(msg, sizeof(msg), "GPIO10 doi - GPIO4 chon (%d)", secs_left);
    } else {
        snprintf(msg, sizeof(msg), "GPIO10 doi - GPIO4 chon");
    }
    display->SetChatMessage("system", msg);
}

bool Run() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    display->SetupUI();  // OledDisplay bo qua neu goi lai lan nua trong Application

    int last = 0;
    {
        Settings settings("eri", false);
        last = settings.GetInt("mode", 0);
    }
    g_sel = (last >= 0 && last < kModeCount) ? last : 0;
    g_touched = false;
    g_confirm = false;
    g_mode = static_cast<int>(Mode::kMenu);
    ESP_LOGI(TAG, "Menu started, last mode=%d", last);

    int secs = kMenuSeconds;
    int elapsed_ms = 0;
    ShowMenu(g_sel, secs);

    while (!g_confirm) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (g_touched) {
            continue;  // da bam nut: cho xac nhan, khong tu vao nua
        }
        elapsed_ms += 100;
        if (elapsed_ms >= 1000) {
            elapsed_ms = 0;
            secs--;
            if (secs <= 0) {
                break;  // het gio: dung che do lan truoc
            }
            ShowMenu(g_sel, secs);
        }
    }

    int sel = g_sel;
    {
        Settings settings("eri", true);
        settings.SetInt("mode", sel);
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
    g_mode = static_cast<int>(Mode::kChat);
    return false;
}

Mode Current() { return static_cast<Mode>(g_mode.load()); }

bool HandleChatClick() {
    ESP_LOGI(TAG, "HandleChatClick mode=%d", (int)Current());
    switch (Current()) {
        case Mode::kMenu:
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
            g_touched = true;
            g_sel = (g_sel.load() + 1) % kModeCount;
            ShowMenu(g_sel, -1);
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
    Board::GetInstance().GetDisplay()->ShowNotification("Ve menu...", 1500);
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
}

}  // namespace EriMode
