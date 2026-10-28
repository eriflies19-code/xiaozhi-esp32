#include "eri_chat_display.h"

#include "application.h"
#include "eri_battery.h"
#include "eri_screen.h"
#include "eri_ui.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <mutex>
#include <string>

#define TAG "EriChat"

namespace {
std::mutex g_mu;
std::string g_status, g_notif, g_emotion = "neutral", g_msg;
bool g_msg_user = false;
int64_t g_notif_until_us = 0;
int64_t g_msg_since_us = 0;

void Fold(const char* in, std::string& out, size_t cap) {
    char buf[700];
    EriUi::FoldAscii(in ? in : "", buf, sizeof(buf));
    out.assign(buf);
    if (out.size() > cap) out.resize(cap);
}

int WifiLevel() {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    int r = ap.rssi;
    return r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : 1;
}
}  // namespace

void EriChatDisplay::SetupUI() {
    OledDisplay::SetupUI();
    if (started_) return;
    started_ = true;
    EriBattery::SetLoadMv(EriBattery::kLoadChatMv);
    EriScreen::Begin();  // dung LVGL, tu ve len panel
    xTaskCreate(TaskEntry, "eri_chat", 4096, this, 3, nullptr);
}

void EriChatDisplay::SetStatus(const char* status) {
    std::lock_guard<std::mutex> l(g_mu);
    Fold(status, g_status, 40);
}

void EriChatDisplay::ShowNotification(const char* n, int duration_ms) {
    std::lock_guard<std::mutex> l(g_mu);
    Fold(n, g_notif, 40);
    g_notif_until_us = esp_timer_get_time() + static_cast<int64_t>(duration_ms) * 1000;
}

void EriChatDisplay::ShowNotification(const std::string& n, int duration_ms) {
    ShowNotification(n.c_str(), duration_ms);
}

bool EriChatDisplay::SetEmotion(const char* emotion) {
    std::lock_guard<std::mutex> l(g_mu);
    g_emotion = emotion ? emotion : "neutral";
    return true;
}

void EriChatDisplay::SetChatMessage(const char* role, const char* content) {
    std::lock_guard<std::mutex> l(g_mu);
    Fold(content, g_msg, 400);
    g_msg_user = role != nullptr && std::string(role) == "user";
    g_msg_since_us = esp_timer_get_time();
}

void EriChatDisplay::RemoveChatMessage(const char*, const char*) {
    std::lock_guard<std::mutex> l(g_mu);
    g_msg.clear();
}

void EriChatDisplay::TaskEntry(void* arg) {
    static_cast<EriChatDisplay*>(arg)->Loop();
}

void EriChatDisplay::Loop() {
    int wifi = 0;
    int wifi_tick = 0;
    for (;;) {
        if (wifi_tick-- <= 0) {
            wifi = WifiLevel();
            wifi_tick = 20;  // 2 giay
        }
        int mode = EriUi::kChatIdle;
        switch (Application::GetInstance().GetDeviceState()) {
            case kDeviceStateListening: mode = EriUi::kChatListening; break;
            case kDeviceStateSpeaking: mode = EriUi::kChatSpeaking; break;
            case kDeviceStateConnecting:
            case kDeviceStateStarting: mode = EriUi::kChatBusy; break;
            default: break;
        }

        std::string status, notif, emo, msg;
        bool user;
        int64_t now = esp_timer_get_time(), since;
        {
            std::lock_guard<std::mutex> l(g_mu);
            status = g_status;
            if (now < g_notif_until_us) notif = g_notif;
            emo = g_emotion;
            msg = g_msg;
            user = g_msg_user;
            since = g_msg_since_us;
        }
        EriUi::ChatView v;
        v.status = status.c_str();
        v.notif = notif.c_str();
        v.emotion = emo.c_str();
        v.msg = msg.c_str();
        v.msg_is_user = user;
        v.wifi = wifi;
        v.batt = EriBattery::Percent();
        v.mode = mode;
        v.t_ms = static_cast<uint32_t>(now / 1000);
        v.msg_age_ms = static_cast<uint32_t>((now - since) / 1000);
        EriUi::DrawChat(v);
        EriScreen::Present();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
