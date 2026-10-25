#pragma once

#include "display/oled_display.h"

// Man hinh Chat AI cho OLED 128x64: thanh tren (WiFi + trang thai),
// giua (bieu cam + song am), duoi (chu AI noi). Ve truc tiep, khong qua LVGL.
class EriChatDisplay : public OledDisplay {
public:
    using OledDisplay::OledDisplay;

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    bool SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void RemoveChatMessage(const char* role, const char* content) override;
    void UpdateStatusBar(bool update_all = false) override {}
    void SetPowerSaveMode(bool on) override {}

private:
    static void TaskEntry(void* arg);
    void Loop();
    bool started_ = false;
};
