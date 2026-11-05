#pragma once
#include <cstddef>
#include <cstdint>
// Cac man hinh (menu, radio, WAZE HUD) ve bang eri_gfx. Khong phu thuoc ESP-IDF.

#include <stdint.h>

namespace EriUi {

// ---- Menu chon che do. sel: 0 Chat AI, 1 Radio, 2 WAZE HUD, 3 Clock, 4 Settings.
// ms_total = 0: dang cho (khong dem nguoc); nguoc lai ve thanh dem nguoc.
void DrawMenu(int sel, int ms_left, int ms_total, int batt = -1,
              const char* clock = nullptr);  // batt: 0..100, -1 = an; clock: chuoi gio, nullptr = an

// ---- Dong ho lon va thoi tiet
struct ClockView {
    bool valid = false;
    int h = 0, m = 0, s = 0;
    int day = 1, mon = 1, year = 2025, wday = 0;  // wday 0 = Chu nhat
    int wifi = 0, batt = -1;
    bool alarm_on = false;       // bao thuc dang bat: hien chuong + gio o thanh tren
    int alarm_h = 0, alarm_m = 0;
};
void DrawClock(const ClockView& v);

// Man hinh cai dat bao thuc. edit: -1 = chi xem, 0 gio, 1 phut, 2 lap lai (dang sua truong do)
struct AlarmView {
    int h = 6, m = 30;
    int repeat = 0;  // 0 mot lan, 1 hang ngay, 2 thu 2 - thu 6
    bool on = false;
    int edit = -1;
    int wifi = 0, batt = -1;
};
void DrawAlarm(const AlarmView& v);
// Man hinh dang reo (nhap nhay). t_ms: thoi gian dang chay
void DrawAlarmRing(int h, int m, uint32_t t_ms);

struct WeatherView {
    bool valid = false;
    int temp = 0, tmin = 0, tmax = 0;  // do C
    int hum = 0, wind = 0, rain = 0;   // %, km/h, % kha nang mua
    int code = 0;                      // ma thoi tiet WMO
    const char* city = "";
    const char* msg = "";              // trang thai khi chua co so lieu
    int age_min = 0;
    int wifi = 0, batt = -1;
};
void DrawWeather(const WeatherView& v);

// Du bao 3 ngay
struct ForecastView {
    bool valid = false;
    const char* city = "";
    const char* msg = "";
    char day[3][4] = {"", "", ""};  // "Mon", ...
    int code[3] = {0, 0, 0};
    int tmax[3] = {0, 0, 0};
    int tmin[3] = {0, 0, 0};
    int wifi = 0, batt = -1;
};
void DrawForecast(const ForecastView& v);

// Thong bao giua man hinh (tat nguon...) + thong bao pin yeu de EriScreen ve de len moi man hinh
void DrawNotice(const char* line1, const char* line2);
void DrawLowBatteryBanner(int pct);
// Logo luc cap nguon (t_ms: thoi gian tu luc bat dau, ~1500ms)
void DrawSplash(uint32_t t_ms, const char* version);

// ---- Man hinh Settings. item: 0 do sang, 1 tat man hinh, 2 WiFi save, 3 thanh pho thoi tiet, 4 tu tat nguon, 5 thong tin
struct SettingsView {
    int item = 0;
    int bright = 0, bright_levels = 5;
    const char* soff = "";   // vd "3 min"
    bool wifi_save = false;
    const char* city = "";
    const char* aoff = "";   // tu tat nguon, vd "30 min"
    const char* version = "";
    int mv = 0;              // dien ap pin (mV)
    const char* ip = "";     // rong = chua ket noi
    int batt = -1;
};
void DrawSettings(const SettingsView& v);

// ---- Radio
enum RadioPhase { kRadioConnecting = 0, kRadioBuffering, kRadioPlaying, kRadioError };
struct RadioView {
    const char* name = "";
    const char* msg = "";   // dong trang thai nho (loi, ...)
    int index = 0;          // 0-based
    int count = 1;
    int volume = 0;         // 0..100
    int phase = kRadioConnecting;
    int level = 0;          // 0..100, muc am luong dang phat (cho thanh nhay)
    const char* title = "";  // ten bai dang phat (ICY), "" = chua co
    const char* ip = "";     // dia chi web control, "" = chua co
    int batt = -1;           // muc pin 0..100, -1 = khong hien
    int wifi = -1;           // 0 = mat WiFi, 1..4 = vach song, -1 = khong hien
    uint32_t t_ms = 0;
    uint32_t since_switch_ms = 100000;  // ms ke tu khi doi dai: < 2000 thi hien so dai + ten to
};
void DrawRadio(const RadioView& v);

// ---- WAZE HUD
struct HudData {
    int nav = 0, spd = 0, lim = 0, over = 0, trn = 0, dst = -1, exitN = 0;
    int avg = 0, avgL = 0, alr = 0, alrD = -1, alrV = 0;
};
struct HudView {
    HudData hud;
    bool fresh = false;      // dang nhan du lieu
    bool connected = false;  // BLE da ket noi
    bool demo = false;
    bool sound_on = true;
    int batt = -1;
    uint32_t t_ms = 0;
};
void DrawHud(const HudView& v);

// ---- Chat AI
enum ChatMode { kChatIdle = 0, kChatListening, kChatSpeaking, kChatBusy };
struct ChatView {
    const char* status = "";   // trang thai (giua thanh tren)
    const char* notif = "";    // thong bao tam (neu co thi thay trang thai)
    const char* emotion = "";  // ten bieu cam cua Xiaozhi: "happy", "sad", ...
    const char* msg = "";      // chu AI noi (da doi sang ASCII)
    bool msg_is_user = false;
    int wifi = 0;              // 0 = mat ket noi, 1..4 = vach song
    int batt = -1;             // muc pin 0..100, -1 = khong hien
    int mode = kChatIdle;
    uint32_t t_ms = 0;
    uint32_t msg_age_ms = 0;   // thoi gian ke tu khi chu thay doi (de lat trang)
    const char* clock = "";    // "HH:MM  dd/mm" (hien thay chu Standby khi dang cho), "" = khong co gio
};
void DrawChat(const ChatView& v);

// Doi chuoi UTF-8 (tieng Viet co dau) sang ASCII khong dau. Ky tu khac (CJK...) bi bo.
void FoldAscii(const char* utf8, char* out, size_t out_size);

}  // namespace EriUi
