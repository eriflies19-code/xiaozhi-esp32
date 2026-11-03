#include "eri_ui.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "eri_ascii_fold.h"
#include "eri_gfx.h"
#include "eri_hud_icons.h"

using namespace EriGfx;

namespace EriUi {

// ================================================================ Menu

// Mui ten chi nut: nut tren (GPIO10) = tam giac huong len, nut duoi (GPIO4) = tam giac huong xuong.
// Hai nut duoc dat nut tren, nut duoi tren thiet bi. Rong 7px, cao 5px, day/dinh tinh theo baseline.
static void TriUp(int x, int baseline) { Tri(x + 3, baseline - 5, x, baseline, x + 6, baseline); }
static void TriDown(int x, int baseline) { Tri(x, baseline - 5, x + 6, baseline - 5, x + 3, baseline); }

// Dong goi y: "^ up   v down". Truyen nullptr de bo 1 ben. Canh giua man hinh.
static void Hints(int baseline, const char* up, const char* down) {
    int wu = up ? 9 + TextWidth(kFontS, up) : 0;
    int wd = down ? 9 + TextWidth(kFontS, down) : 0;
    int gap = (up && down) ? 10 : 0;
    int x = (kW - (wu + gap + wd)) / 2;
    if (up) {
        TriUp(x, baseline);
        Text(kFontS, x + 9, baseline, up);
        x += wu + gap;
    }
    if (down) {
        TriDown(x, baseline);
        Text(kFontS, x + 9, baseline, down);
    }
}

// Chuong nho 7x8 (bao thuc)
static void DrawBell(int cx, int y) {
    Tri(cx, y, cx - 3, y + 5, cx + 3, y + 5);
    Box(cx - 3, y + 5, 7, 1);
    Pix(cx, y + 7);
    Pix(cx, y);
}

// Bieu tuong pin 15x7 (than 13 + dau cuc 2), ben phai ket thuc o `rx`. Co the kem so %.
// Pin <= 10% thi nhap nhay phan ruot (khi co t_ms).
static void DrawBattery(int rx, int y, int pct, bool text, uint32_t t_ms) {
    if (pct < 0) {
        return;
    }
    pct = std::clamp(pct, 0, 100);
    const int x = rx - 14;
    Frame(x, y, 13, 7);
    Box(x + 13, y + 2, 2, 3);
    int fw = (pct * 9 + 50) / 100;
    if (pct > 0 && fw == 0) {
        fw = 1;
    }
    bool off = pct <= 10 && t_ms != 0 && ((t_ms / 500) % 2) == 1;
    if (!off) {
        Box(x + 2, y + 2, fw, 3);
    }
    if (text) {
        char b[16];
        snprintf(b, sizeof(b), "%d", pct);
        TextR(kFontS, x - 3, y + 6, b);
    }
}

// Bieu tuong song WiFi 4 vach (rong 10px, cao toi da 8px, day o y+... = baseline). level 0 = mat ket noi.
static void DrawWifi(int x, int baseline, int level) {
    if (level < 0) {
        return;
    }
    for (int b = 0; b < 4; ++b) {
        int h = 2 + b * 2;
        if (b < level) {
            Box(x + b * 3, baseline - h + 1, 2, h);
        } else {
            Pix(x + b * 3, baseline);
            Pix(x + b * 3 + 1, baseline);
        }
    }
    if (level == 0) {
        Line(x + 12, baseline - 6, x + 16, baseline);  // dau X: mat ket noi
        Line(x + 16, baseline - 6, x + 12, baseline);
    }
}

// Bieu tuong dong ho: mat tron + 2 kim
static void DrawClockIcon(int cx, int cy) {
    Ring(cx, cy, 11, 2);
    Line(cx, cy, cx, cy - 7);
    Line(cx + 1, cy, cx + 1, cy - 7);
    Line(cx, cy, cx + 5, cy + 3);
    Line(cx, cy + 1, cx + 5, cy + 4);
    Box(cx - 1, cy - 1, 3, 3);
}

// Banh rang (cai dat): than tron + 8 rang + lo giua. fg = mau dang ve
static void DrawGear(int cx, int cy, bool inverted) {
    (void)inverted;
    static const int8_t kDir[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
    for (int i = 0; i < 8; ++i) {
        int dx = kDir[i][0], dy = kDir[i][1];
        int d = (dx != 0 && dy != 0) ? 6 : 9;  // rang cheo nam gan hon cho tron
        int px = cx + dx * d, py = cy + dy * d;
        Box(px - 2, py - 2, 5, 5);  // rang
    }
    Ring(cx, cy, 8, 4);  // than
    // lo giua giu nguyen nen (xoa mau nguoc)
}

void DrawMenu(int sel, int ms_left, int ms_total, int batt, const char* clock) {
    Clear();
    SetColor(true);

    // Dong tren: dong ho (trai), pin (phai)
    if (clock != nullptr && clock[0]) {
        Text(kFontS, 2, 8, clock);
    }
    DrawBattery(127, 1, batt, true, 0);

    static const Bmp* const kIcons[3] = {&icChat, &icRadio, &icNav};
    static const char* const kNames[5] = {"CHAT AI", "RADIO", "WAZE", "CLOCK", "SET"};
    const int n = 5, tw = 36, th = 36, gap = 4, ty = 11;
    const int x0 = (kW - (3 * tw + 2 * gap)) / 2;
    int first = std::clamp(sel - 1, 0, n - 3);  // cua so 3 o, cuon theo o dang chon

    for (int k = 0; k < 3; ++k) {
        int i = first + k;
        int x = x0 + k * (tw + gap);
        bool on = (i == sel);
        SetColor(true);
        if (on) {
            RoundBox(x, ty, tw, th, 5);
        } else {
            RoundFrame(x, ty, tw, th, 5);
        }
        SetColor(!on);
        if (i < 3) {
            Draw(*kIcons[i], x + (tw - kIcons[i]->w) / 2, ty + 3);
        } else if (i == 3) {
            DrawClockIcon(x + tw / 2, ty + 15);
        } else {
            DrawGear(x + tw / 2, ty + 15, on);
        }
        TextC(kFontS, x + tw / 2, ty + 33, kNames[i]);
    }
    // Mui ten bao con o ben trai/phai
    SetColor(true);
    if (first > 0) {
        Line(4, 29, 1, 29);
        Line(3, 27, 3, 31);
        Line(2, 28, 2, 30);
    }
    if (first + 3 < n) {
        Line(123, 29, 126, 29);
        Line(124, 27, 124, 31);
        Line(125, 28, 125, 30);
    }
    SetColor(true);

    if (ms_total > 0) {
        int w = 124 * std::max(ms_left, 0) / ms_total;
        Frame(2, 59, 124, 4);
        Box(2, 59, w, 4);
        Hints(55, "next", "select");
    } else {
        Hints(58, "next", "select");
    }
}


// ================================================================ Dong ho / Thoi tiet

void DrawClock(const ClockView& v) {
    Clear();
    SetColor(true);
    DrawWifi(2, 9, v.wifi);
    DrawBattery(127, 1, v.batt, true, 0);
    if (v.alarm_on) {
        char a[8];
        snprintf(a, sizeof(a), "%02d:%02d", v.alarm_h % 100, v.alarm_m % 100);
        DrawBell(34, 1);
        Text(kFontS, 41, 8, a);
    }

    // Doi vi tri 1px moi phut de OLED khong bi in bong
    int dx = v.valid ? (v.m % 3) - 1 : 0;
    int dy = v.valid ? ((v.m / 3) % 3) - 1 : 0;
    char hh[4], mm[4];
    snprintf(hh, sizeof(hh), "%02d", v.valid ? v.h % 100 : 0);
    snprintf(mm, sizeof(mm), "%02d", v.valid ? v.m % 100 : 0);
    const int w2 = TextWidth(kFontXL, "00");
    const int cg = 12;  // khoang cho dau hai cham
    int x0 = (kW - (2 * w2 + cg)) / 2 + dx;
    int base = 45 + dy;
    if (v.valid) {
        Text(kFontXL, x0, base, hh);
        Text(kFontXL, x0 + w2 + cg, base, mm);
        if (v.s % 2 == 0) {  // dau hai cham nhap nhay moi giay
            int cx = x0 + w2 + cg / 2 - 2;
            Box(cx - 1, base - 20, 3, 3);
            Box(cx - 1, base - 9, 3, 3);
        }
        static const char* const kDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        char d[40];
        snprintf(d, sizeof(d), "%s  %02d/%02d/%04d", kDays[((v.wday % 7) + 7) % 7], v.day % 100, v.mon % 100,
                 v.year % 10000);
        TextC(kFontM, 64 + dx, 62, d);
    } else {
        TextC(kFontXL, 64, base, "--:--");
        TextC(kFontM, 64, 62, "Syncing time...");
    }
}

// ---- Bieu tuong thoi tiet (nhom theo ma WMO)
static void Disc(int cx, int cy, int r) {
    Ring(cx, cy, r, r);
    Pix(cx, cy);
}

static void CloudShape(int cx, int cy, int g) {
    Disc(cx - 8, cy + 3, 6 + g);
    Disc(cx + 1, cy - 3, 8 + g);
    Disc(cx + 9, cy + 3, 6 + g);
    Box(cx - 8, cy + 3 - g, 18, 6 + g * 2);
}

// Dam may co vien den xung quanh de de len mat troi
static void Cloud(int cx, int cy) {
    SetColor(false);
    CloudShape(cx, cy, 2);
    SetColor(true);
    CloudShape(cx, cy, 0);
}

static void Sun(int cx, int cy, int r) {
    Disc(cx, cy, r);
    static const int8_t kD[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
    for (int i = 0; i < 8; ++i) {
        int dx = kD[i][0], dy = kD[i][1];
        int a = r + 3, b = r + 6;
        if (dx != 0 && dy != 0) {
            a = (r + 3) * 7 / 10;
            b = (r + 6) * 7 / 10;
        }
        Line(cx + dx * a, cy + dy * a, cx + dx * b, cy + dy * b);
    }
}

static void WeatherIcon(int cx, int cy, int code) {
    SetColor(true);
    if (code == 0) {
        Sun(cx, cy, 8);
    } else if (code <= 2) {
        Sun(cx - 7, cy - 7, 5);
        Cloud(cx + 4, cy + 3);
    } else if (code == 3) {
        Cloud(cx, cy);
    } else if (code == 45 || code == 48) {
        for (int i = 0; i < 4; ++i) {
            Line(cx - 14 + (i % 2) * 3, cy - 9 + i * 6, cx + 12 + (i % 2) * 3, cy - 9 + i * 6);
        }
    } else if (code >= 95) {
        Cloud(cx, cy - 6);
        Line(cx + 2, cy + 4, cx - 3, cy + 11);  // tia set
        Line(cx + 3, cy + 4, cx - 2, cy + 11);
        Line(cx - 3, cy + 11, cx + 3, cy + 11);
        Line(cx + 3, cy + 11, cx - 2, cy + 18);
        Line(cx + 4, cy + 11, cx - 1, cy + 18);
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        Cloud(cx, cy - 6);
        for (int i = 0; i < 3; ++i) {  // bong tuyet
            int x = cx - 8 + i * 8, y = cy + 10 + (i % 2) * 4;
            Line(x - 2, y, x + 2, y);
            Line(x, y - 2, x, y + 2);
        }
    } else {
        Cloud(cx, cy - 6);
        for (int i = 0; i < 4; ++i) {  // hat mua
            int x = cx - 10 + i * 7, y = cy + 6 + (i % 2) * 3;
            Line(x, y, x - 2, y + 6);
        }
    }
}

static const char* WeatherText(int c) {
    if (c == 0) return "Clear";
    if (c <= 2) return "Partly cloudy";
    if (c == 3) return "Cloudy";
    if (c == 45 || c == 48) return "Fog";
    if (c >= 51 && c <= 57) return "Drizzle";
    if (c >= 61 && c <= 67) return "Rain";
    if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "Snow";
    if (c >= 80 && c <= 82) return "Showers";
    if (c >= 95) return "Storm";
    return "Cloudy";
}

void DrawWeather(const WeatherView& v) {
    Clear();
    SetColor(true);
    DrawWifi(2, 9, v.wifi);
    TextC(kFontS, 64, 8, v.city);
    DrawBattery(127, 1, v.batt, true, 0);

    if (!v.valid) {
        TextC(kFontM, 64, 36, v.msg[0] ? v.msg : "Loading weather...");
        Hints(62, "refresh", "clock");
        return;
    }
    WeatherIcon(22, 28, v.code);
    char b[32];
    snprintf(b, sizeof(b), "%d", v.temp);
    int tw = TextWidth(kFontXL, b);
    int tx = 46;
    Text(kFontXL, tx, 42, b);
    Ring(tx + tw + 5, 17, 3, 1);   // dau do
    Text(kFontM, tx + tw + 10, 30, "C");
    // Mo ta + cao/thap
    Text(kFontM, 46, 54, WeatherText(v.code));
    snprintf(b, sizeof(b), "%d/%d", v.tmax, v.tmin);
    TextR(kFontS, 126, 33, b);
    char d[40];
    snprintf(d, sizeof(d), "Hum %d%%  Wind %d  Rain %d%%", v.hum, v.wind, v.rain);
    TextC(kFontS, 64, 63, d);
}

static const char* RepeatText(int r) { return r == 1 ? "Daily" : r == 2 ? "Mon-Fri" : "Once"; }

void DrawAlarm(const AlarmView& v) {
    Clear();
    SetColor(true);
    DrawBattery(127, 1, v.batt, true, 0);
    if (v.edit >= 0) {
        Text(kFontS, 2, 8, "Set alarm");
    } else {
        DrawBell(5, 1);
        Text(kFontS, 12, 8, "Alarm");
        Text(kFontS, 44, 8, "hold");  // giu nut tren de sua
        TriUp(66, 8);
        Text(kFontS, 75, 8, "edit");
    }

    char hh[4], mm[4];
    snprintf(hh, sizeof(hh), "%02d", v.h % 100);
    snprintf(mm, sizeof(mm), "%02d", v.m % 100);
    const int w2 = TextWidth(kFontXL, "00");
    const int cg = 12;
    int x0 = (kW - (2 * w2 + cg)) / 2;
    const int base = 43;
    // Truong dang sua: ve dao mau trong o chu nhat
    if (v.edit == 0) {
        RoundBox(x0 - 3, base - 32, w2 + 6, 36, 3);
        SetColor(false);
    }
    Text(kFontXL, x0, base, hh);
    SetColor(true);
    if (v.edit == 1) {
        RoundBox(x0 + w2 + cg - 3, base - 32, w2 + 6, 36, 3);
        SetColor(false);
    }
    Text(kFontXL, x0 + w2 + cg, base, mm);
    SetColor(true);
    int cx = x0 + w2 + cg / 2 - 2;
    Box(cx - 1, base - 20, 3, 3);
    Box(cx - 1, base - 9, 3, 3);

    // Dong duoi: lap lai (trai), bat/tat (phai)
    if (v.edit == 2) {
        int w = TextWidth(kFontS, RepeatText(v.repeat));
        RoundBox(1, 46, w + 6, 10, 2);
        SetColor(false);
        Text(kFontS, 4, 54, RepeatText(v.repeat));
        SetColor(true);
    } else {
        Text(kFontS, 4, 54, RepeatText(v.repeat));
    }
    if (v.on) {
        RoundBox(104, 46, 22, 10, 2);
        SetColor(false);
        TextR(kFontS, 123, 54, "ON");
        SetColor(true);
    } else {
        RoundFrame(104, 46, 22, 10, 2);
        TextR(kFontS, 123, 54, "OFF");
    }
    if (v.edit >= 0) {
        Hints(63, "change", v.edit == 2 ? "done" : "next");
    } else {
        Hints(63, "on/off", "next");
    }
}

void DrawAlarmRing(int h, int m, uint32_t t_ms) {
    Clear();
    bool inv = ((t_ms / 500) % 2) == 0;
    SetColor(true);
    if (inv) {
        Box(0, 0, kW, kH);
        SetColor(false);
    }
    DrawBell(40, 3);
    Text(kFontM, 50, 14, "ALARM");
    char hh[4], mm[4];
    snprintf(hh, sizeof(hh), "%02d", h % 100);
    snprintf(mm, sizeof(mm), "%02d", m % 100);
    const int w2 = TextWidth(kFontXL, "00");
    const int cg = 12;
    int x0 = (kW - (2 * w2 + cg)) / 2;
    Text(kFontXL, x0, 50, hh);
    Text(kFontXL, x0 + w2 + cg, 50, mm);
    int cx = x0 + w2 + cg / 2 - 2;
    Box(cx - 1, 50 - 20, 3, 3);
    Box(cx - 1, 50 - 9, 3, 3);
    // goi y trong vien duoi: dung Hints voi mau hien tai
    Hints(63, "snooze", "stop");
    SetColor(true);
}

// ================================================================ Settings

void DrawSettings(const SettingsView& v) {
    Clear();
    SetColor(true);
    Text(kFontS, 2, 8, v.item == 4 ? "INFO" : "Settings");
    DrawBattery(127, 1, v.batt, true, 0);

    if (v.item == 4) {
        char b[48];
        Text(kFontS, 2, 24, "Firmware");
        TextR(kFontS, 126, 24, v.version);
        Text(kFontS, 2, 35, "Battery");
        snprintf(b, sizeof(b), "%d.%02d V", v.mv / 1000, (v.mv % 1000) / 10);
        TextR(kFontS, 126, 35, v.mv > 0 ? b : "-");
        Text(kFontS, 2, 46, "Web IP");
        TextR(kFontS, 126, 46, v.ip[0] ? v.ip : "open Radio");
        Hints(62, nullptr, "back to menu");
        return;
    }

    static const char* const kLabels[4] = {"Brightness", "Screen off", "WiFi power save", "Weather city"};
    for (int i = 0; i < 4; ++i) {
        int base = 19 + i * 10;
        bool on = (i == v.item);
        if (on) {
            RoundBox(1, base - 8, 126, 10, 2);
            SetColor(false);
        }
        Text(kFontS, 4, base, kLabels[i]);
        if (i == 0) {
            // 5 vach cao dan; vach <= muc hien tai dac
            int n = v.bright_levels;
            for (int k = 0; k < n; ++k) {
                int h = 2 + k * 3 / 2;
                int x = 126 - (n - k) * 5;
                if (k <= v.bright) {
                    Box(x, base + 1 - h, 3, h);
                } else {
                    Box(x, base, 3, 1);
                }
            }
        } else if (i == 1) {
            TextR(kFontS, 124, base, v.soff);
        } else if (i == 3) {
            TextR(kFontS, 124, base, v.city);
        } else {
            TextR(kFontS, 124, base, v.wifi_save ? "On" : "Off");
        }
        SetColor(true);
    }
    Hints(62, "change", "next");
}

// ================================================================ Radio

static void Marquee(const char* s, int baseline, uint32_t t_ms) {
    int w = TextWidth(kFontM, s);
    const int avail = kW - 4;
    if (w <= avail) {
        TextC(kFontM, kW / 2, baseline, s);
        return;
    }
    const int gap = 30;
    int cycle = w + gap;
    int off = static_cast<int>((t_ms / 35) % static_cast<uint32_t>(cycle));  // ~28 px/giay
    // Cho 1 giay dau doc duoc phan dau chu
    if (t_ms % (cycle * 35 + 1000) < 1000) {
        off = 0;
    }
    Text(kFontM, 2 - off, baseline, s);
    Text(kFontM, 2 - off + cycle, baseline, s);
}

// Chu nho: vua man hinh thi canh giua, dai hon thi chay ngang
static void MarqueeS(const char* s, int baseline, uint32_t t_ms) {
    int w = TextWidth(kFontS, s);
    if (w <= 124) {
        TextC(kFontS, 64, baseline, s);
        return;
    }
    const int cycle = w + 30;
    int off = static_cast<int>((t_ms / 45) % static_cast<uint32_t>(cycle));
    if (t_ms % (static_cast<uint32_t>(cycle) * 45 + 1200) < 1200) {
        off = 0;  // dung 1,2 giay o dau cau
    }
    Text(kFontS, 2 - off, baseline, s);
    Text(kFontS, 2 - off + cycle, baseline, s);
}

void DrawRadio(const RadioView& v) {
    Clear();
    SetColor(true);

    // Dau trang
    Draw(icNote, 1, 1);
    Text(kFontS, 13, 9, "RADIO");
    char idx[16];
    snprintf(idx, sizeof(idx), "%d/%d", v.index + 1, v.count);
    TextR(kFontS, 126, 9, idx);
    DrawBattery(96, 2, v.batt, true, v.t_ms);
    DrawWifi(46, 9, v.wifi);

    // Ten dai
    Marquee(v.name, 27, v.t_ms);

    // Dong trang thai
    const char* st = v.msg;
    char web[32];
    if (st == nullptr || st[0] == 0) {
        st = v.phase == kRadioPlaying ? "Playing"
             : v.phase == kRadioBuffering ? "Buffering..."
             : v.phase == kRadioError ? "Error"
                                      : "Connecting...";
        if (v.phase == kRadioPlaying) {
            if (v.ip != nullptr && v.ip[0] && (v.t_ms % 16000) < 3500) {
                snprintf(web, sizeof(web), "Web: %s", v.ip);  // thinh thoang hien dia chi web
                st = web;
            } else if (v.title != nullptr && v.title[0]) {
                st = v.title;
            }
        }
    }
    MarqueeS(st, 38, v.t_ms);

    // Thanh nhay am thanh: 16 cot
    const int base = 52, maxh = 11;
    for (int i = 0; i < 16; ++i) {
        int h = 1;
        if (v.phase == kRadioPlaying) {
            // doi cot theo muc am luong + dao dong gia lap rieng tung cot
            uint32_t k = (v.t_ms / 90) * 7919u + static_cast<uint32_t>(i) * 104729u;
            k ^= k >> 7;
            k *= 2654435761u;
            k ^= k >> 15;
            int wob = static_cast<int>(k % 100);
            int lvl = std::min(v.level, 100);
            h = 1 + (maxh - 1) * (lvl * (45 + wob * 55 / 100) / 100) / 100;
            h = std::min(h, maxh);
        } else if (v.phase == kRadioBuffering || v.phase == kRadioConnecting) {
            h = ((v.t_ms / 120 + i) % 16 < 3) ? 4 : 1;  // song cho
        }
        Box(i * 8 + 1, base - h + 1, 6, h);
    }

    // Am luong
    Draw(v.volume > 0 ? icSpk : icMute, 1, 54);
    Frame(14, 57, 88, 5);
    Box(14, 57, 88 * std::clamp(v.volume, 0, 100) / 100, 5);
    char vb[8];
    snprintf(vb, sizeof(vb), "%d", v.volume);
    TextR(kFontS, 126, 62, vb);
}

// ================================================================ WAZE HUD

struct AlertIcon {
    const uint8_t* bmp;
    int maxM;
    bool speedNum;
};

static AlertIcon AlertIconFor(int a) {
    switch (a) {
        case 1: return {ICON_POLICE, 500, false};
        case 2: case 40: case 41: case 42: case 43: case 44: case 45: case 46:
            return {ICON_CAM, 500, false};
        case 3: return {ICON_LIGHT, 500, false};
        case 4: case 13: case 14: case 15: case 17: case 48:
            return {ICON_HAZARD, 500, false};
        case 5: return {ICON_CRASH, 500, false};
        case 6: return {ICON_JAM, 500, false};
        case 7: return {ICON_CLOSED, 500, false};
        case 9: return {ICON_NOPASS, 500, false};
        case 11: return {ICON_RAIL, 500, false};
        case 12: return {ICON_TOLL, 500, false};
        case 20: case 21: return {ICON_REST, 500, false};
        case 8: return {nullptr, 200, true};
        case 23: return {ICON_KDC_START, 200, false};
        case 24: return {ICON_KDC_END, 200, false};
        case 28: return {ICON_NOLEFT, 200, false};
        case 29: return {ICON_NORIGHT, 200, false};
        case 30: return {ICON_NOUTURN, 200, false};
        case 57: return {ICON_SCHOOL, 200, false};
        case 59: return {ICON_CURVE, 200, false};
        default: return {nullptr, 0, false};
    }
}

static void FmtDist(int m, char* out, size_t sz) {
    if (m < 1000) {
        snprintf(out, sz, "%dm", m);
    } else {
        snprintf(out, sz, "%d.%dkm", m / 1000, (m % 1000) / 100);
    }
}

static void Bx(int cx, bool m, int x, int y, int w, int h) {
    if (m) {
        x = 2 * cx - x - w + 1;
    }
    Box(x, y, w, h);
}

static void Tr(int cx, bool m, int x1, int y1, int x2, int y2, int x3, int y3) {
    if (m) {
        x1 = 2 * cx - x1;
        x2 = 2 * cx - x2;
        x3 = 2 * cx - x3;
    }
    Tri(x1, y1, x2, y2, x3, y3);
}

static void DrawTurn(int cx, int cy, int trn, int ex) {
    bool left = (trn == 2 || trn == 4 || trn == 6 || trn == 13 || trn == 15);
    bool right = (trn == 3 || trn == 5 || trn == 7 || trn == 14 || trn == 16);
    if (left || right) {
        bool m = right;
        Bx(cx, m, cx + 2, cy - 2, 4, 14);
        Bx(cx, m, cx - 6, cy - 5, 12, 4);
        Tr(cx, m, cx - 12, cy - 3, cx - 4, cy - 11, cx - 4, cy + 5);
    } else if (trn == 1 || trn == 19) {
        Box(cx - 2, cy - 4, 4, 16);
        Tri(cx, cy - 12, cx - 8, cy - 2, cx + 8, cy - 2);
    } else if (trn == 8 || trn == 9) {
        Box(cx + 4, cy - 4, 4, 16);
        Box(cx - 7, cy - 8, 15, 4);
        Box(cx - 7, cy - 8, 4, 12);
        Tri(cx - 5, cy + 12, cx - 13, cy + 3, cx + 3, cy + 3);
    } else if (trn == 10 || trn == 11 || trn == 12 || trn == 20) {
        Ring(cx, cy, 12, 2);
        if (ex > 0) {
            char b[16];
            snprintf(b, sizeof(b), "%d", ex);
            TextC(kFontM, cx, cy + CapHeight(kFontM) / 2, b);
        }
    } else if (trn == 17) {
        Box(cx - 7, cy - 11, 2, 23);
        Frame(cx - 5, cy - 11, 13, 9);
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 3; ++c) {
                if (((r + c) & 1) == 0) {
                    Box(cx - 4 + c * 4, cy - 10 + r * 4, 4, 4);
                }
            }
        }
    }
}

static void DrawSpeedSign(int cx, int cy, int v) {
    Ring(cx, cy, 14, 3);
    if (v <= 0) {
        return;
    }
    char b[16];
    snprintf(b, sizeof(b), "%d", v);
    TextC(kFontM, cx, cy + CapHeight(kFontM) / 2, b);
}

void DrawHud(const HudView& v) {
    Clear();
    SetColor(true);

    if (!v.fresh) {
        Draw(icNav, 52, 4);
        TextC(kFontM, 64, 42, "WAZE HUD");
        TextC(kFontS, 64, 56, v.connected ? "Waiting for Waze..." : "Waiting for BLE...");
        DrawBattery(127, 1, v.batt, true, v.t_ms);
        if (v.demo) {
            Text(kFontS, 1, 62, "DEMO");
        }
        return;
    }

    const HudData& h = v.hud;
    char b[32];

    // ---- Bien gioi han toc do (trai). Vuot toc do thi nhap nhay dac/dao mau.
    bool over = h.over || (h.lim > 0 && h.spd > h.lim);
    bool inv = over && ((v.t_ms / 400) % 2 == 0);
    const int LX = 27, LY = 31, LR = 26;
    if (inv) {
        Disc(LX, LY, LR);
    } else {
        Ring(LX, LY, LR, 4);
    }
    SetColor(!inv);
    if (h.lim > 0) {
        snprintf(b, sizeof(b), "%d", h.lim);
        const Font& f = strlen(b) >= 3 ? kFontL : kFontXL;
        TextC(f, LX, LY + CapHeight(f) / 2, b);
    } else {
        TextC(kFontXL, LX, LY + CapHeight(kFontXL) / 2, "-");
    }
    SetColor(true);

    // ---- Toc do hien tai (phai tren) + trang thai
    snprintf(b, sizeof(b), "%d", h.spd);
    const int unit_w = TextWidth(kFontS, "km/h");
    TextR(kFontS, 126, 19, "km/h");
    TextR(kFontL, 126 - unit_w - 3, 19, b);
    if (v.connected) {
        Draw(icBt, 59, 2);
    }
    if (!v.sound_on) {
        Draw(icMute, 59, 16);
    }
    if (v.demo) {
        Text(kFontS, 59, 10, "DEMO");  // nho, de nhan biet dang chay du lieu gia
    }
    DrawBattery(127, 1, v.batt, false, v.t_ms);

    // ---- Canh bao (uu tien) hoac mui ten re
    AlertIcon ai = {nullptr, 0, false};
    int adist = -1;
    if (h.avg) {
        ai = {ICON_NOPASS, 100000, false};
        adist = h.avgL;
    } else if (h.alr > 0 && h.alrD >= 0) {
        AlertIcon t = AlertIconFor(h.alr);
        if ((t.bmp || t.speedNum) && h.alrD <= t.maxM) {
            ai = t;
            adist = h.alrD;
        }
    }

    char d[32];
    const int CX = 93;
    if (ai.bmp || ai.speedNum) {
        if (ai.speedNum) {
            DrawSpeedSign(CX, 33, h.alrV);
        } else {
            DrawXbm(ai.bmp, CX - 12, 22, 24);
        }
        FmtDist(adist, d, sizeof(d));
        TextC(kFontL, CX, 62, d);
    } else if (h.nav) {
        DrawTurn(CX, 33, h.trn, h.exitN);
        if (h.dst >= 0) {
            FmtDist(h.dst, d, sizeof(d));
            TextC(kFontL, CX, 62, d);
        }
    }
}

// ================================================================ Chat AI

void FoldAscii(const char* s, char* out, size_t cap) {
    size_t n = 0;
    auto put = [&](char ch) {
        if (n + 1 < cap) {
            out[n++] = ch;
        }
    };
    while (*s) {
        unsigned char c = static_cast<unsigned char>(*s);
        uint32_t cp = 0;
        int len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            len = 4;
        }
        for (int i = 1; i < len; ++i) {
            unsigned char cc = static_cast<unsigned char>(s[i]);
            if ((cc & 0xC0) != 0x80) {
                len = i;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        s += len;
        if (cp < 0x80) {
            put(static_cast<char>(cp == '\n' || cp == '\r' || cp == '\t' ? ' ' : cp));
        } else if (cp == 0x2026) {
            put('.');
            put('.');
            put('.');
        } else {
            int lo = 0, hi = kFoldCount - 1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (kFoldCp[mid] == cp) {
                    put(kFoldCh[mid]);
                    break;
                }
                if (kFoldCp[mid] < cp) {
                    lo = mid + 1;
                } else {
                    hi = mid - 1;
                }
            }
        }
    }
    if (cap > 0) {
        out[n] = 0;
    }
}

enum Face { kFNeutral, kFHappy, kFLaugh, kFSad, kFCry, kFAngry, kFSurprised, kFThink, kFSleepy,
            kFWink, kFCool, kFLove };

static int FaceFor(const char* e) {
    struct M { const char* n; int f; };
    static const M kMap[] = {
        {"neutral", kFNeutral}, {"relaxed", kFNeutral}, {"happy", kFHappy}, {"delicious", kFHappy},
        {"kissy", kFHappy}, {"confident", kFHappy}, {"laughing", kFLaugh}, {"funny", kFLaugh},
        {"sad", kFSad}, {"crying", kFCry}, {"angry", kFAngry}, {"surprised", kFSurprised},
        {"shocked", kFSurprised}, {"thinking", kFThink}, {"confused", kFThink},
        {"embarrassed", kFThink}, {"sleepy", kFSleepy}, {"winking", kFWink}, {"silly", kFWink},
        {"cool", kFCool}, {"loving", kFLove},
    };
    for (const auto& m : kMap) {
        if (strcmp(e, m.n) == 0) {
            return m.f;
        }
    }
    return kFNeutral;
}

// Net day 2px
static void Line2(int x0, int y0, int x1, int y1) {
    Line(x0, y0, x1, y1);
    Line(x0, y0 + 1, x1, y1 + 1);
}

static void Heart(int cx, int cy) {
    Disc(cx - 2, cy - 1, 2);
    Disc(cx + 2, cy - 1, 2);
    Tri(cx - 4, cy, cx + 4, cy, cx, cy + 5);
}

static void Smile(int cx, int cy, bool open) {
    Line2(cx - 7, cy, cx - 4, cy + 3);
    Line2(cx - 4, cy + 3, cx + 4, cy + 3);
    Line2(cx + 4, cy + 3, cx + 7, cy);
    if (open) {
        RoundBox(cx - 5, cy + 1, 11, 6, 3);
    }
}

static void DrawFace(int emo, int cx, int cy, uint32_t t, bool speaking) {
    Ring(cx, cy, 15, 2);                       // dau tron
    Box(cx - 1, cy - 18, 2, 3);                // than ang-ten
    Disc(cx, cy - 19, 2);                      // dau ang-ten
    RoundBox(cx - 19, cy - 4, 4, 9, 2);        // tai nghe trai
    RoundBox(cx + 16, cy - 4, 4, 9, 2);        // tai nghe phai
    const int ex = 8, ey = cy - 4;  // lech mat
    bool blink = (t % 3600) < 130 && emo != kFSleepy;

    // Mat
    switch (emo) {
        case kFHappy:
        case kFLaugh:
        case kFLove:
            if (emo == kFLove) {
                Heart(cx - ex, ey);
                Heart(cx + ex, ey);
            } else {
                Line2(cx - ex - 3, ey + 2, cx - ex, ey - 2);
                Line2(cx - ex, ey - 2, cx - ex + 3, ey + 2);
                Line2(cx + ex - 3, ey + 2, cx + ex, ey - 2);
                Line2(cx + ex, ey - 2, cx + ex + 3, ey + 2);
            }
            break;
        case kFSurprised:
            Ring(cx - ex, ey, 4, 2);
            Ring(cx + ex, ey, 4, 2);
            break;
        case kFSleepy:
            Box(cx - ex - 3, ey + 1, 7, 2);
            Box(cx + ex - 3, ey + 1, 7, 2);
            break;
        case kFCool:
            Box(cx - 11, ey - 3, 9, 6);
            Box(cx + 2, ey - 3, 9, 6);
            Box(cx - 2, ey - 2, 4, 2);
            break;
        case kFWink:
            RoundBox(cx - ex - 2, ey - 4, 5, 8, 2);
            Line2(cx + ex - 3, ey - 2, cx + ex + 3, ey);
            Line2(cx + ex + 3, ey, cx + ex - 3, ey + 2);
            break;
        case kFAngry:
            RoundBox(cx - ex - 2, ey - 2, 5, 6, 2);
            RoundBox(cx + ex - 2, ey - 2, 5, 6, 2);
            Line2(cx - ex - 2, ey - 6, cx - ex + 3, ey - 4);
            Line2(cx + ex + 2, ey - 6, cx + ex - 3, ey - 4);
            break;
        case kFThink:
            RoundBox(cx - ex - 2, ey - 4, 5, 8, 2);
            RoundBox(cx + ex - 2, ey - 4, 5, 8, 2);
            Line(cx + ex - 3, ey - 7, cx + ex + 2, ey - 8);
            break;
        default:  // neutral, sad, cry
            if (blink) {
                Box(cx - ex - 2, ey, 5, 2);
                Box(cx + ex - 2, ey, 5, 2);
            } else {
                RoundBox(cx - ex - 2, ey - 4, 5, 8, 2);
                RoundBox(cx + ex - 2, ey - 4, 5, 8, 2);
            }
            break;
    }
    if (emo == kFCry) {
        int drop = static_cast<int>((t / 120) % 8);
        Box(cx - ex, ey + 5 + drop, 2, 2);
        Box(cx + ex, ey + 5 + (drop + 3) % 8, 2, 2);
    }

    // Mieng
    const int my = cy + 6;
    if (speaking) {
        int open = static_cast<int>((t / 110) % 4);
        static const int hs[4] = {2, 5, 7, 4};
        RoundBox(cx - 5, my - hs[open] / 2, 11, hs[open], 2);
        return;
    }
    switch (emo) {
        case kFHappy:
        case kFWink:
        case kFCool:
        case kFLove:
            Smile(cx, my - 2, false);
            break;
        case kFLaugh:
            Smile(cx, my - 3, true);
            break;
        case kFSad:
        case kFCry:
            Line2(cx - 6, my + 3, cx - 3, my);
            Line2(cx - 3, my, cx + 3, my);
            Line2(cx + 3, my, cx + 6, my + 3);
            break;
        case kFAngry:
            Box(cx - 5, my, 11, 2);
            break;
        case kFSurprised:
            Ring(cx, my + 1, 3, 2);
            break;
        case kFThink:
            Line2(cx - 3, my + 1, cx + 6, my - 1);
            break;
        case kFSleepy:
            Box(cx - 3, my, 7, 2);
            Text(kFontS, cx + 21, cy - 8 - static_cast<int>((t / 300) % 3), "z");
            break;
        default:
            Box(cx - 5, my, 11, 2);
            break;
    }
}

static void DrawWaves(int cx, int cy, int mode, uint32_t t) {
    const int n = 7;  // so vach moi ben, k = 0 la gan dau
    for (int k = 0; k < n; ++k) {
        int h = 2;
        if (mode == kChatListening || mode == kChatSpeaking) {
            uint32_t q = (t / 70) * 7919u + static_cast<uint32_t>(k) * 104729u;
            q ^= q >> 7;
            q *= 2654435761u;
            q ^= q >> 15;
            int r = static_cast<int>(q % 100);
            int env = 100 - k * 9;
            int amp = mode == kChatSpeaking ? 28 : 16;
            h = 2 + amp * r / 100 * env / 100;
        } else if (mode == kChatBusy) {
            h = (static_cast<int>(t / 110) % (n + 3) == (n - 1 - k)) ? 8 : 2;
        }
        h &= ~1;
        if (h < 2) {
            h = 2;
        }
        int x = cx - 24 - k * 5;
        Box(x, cy - h / 2, 3, h);
        Box(2 * cx - 3 - x, cy - h / 2, 3, h);
    }
}

static int WrapLines(const char* s, int maxw, int* starts, int* lens, int max_lines) {
    int n = 0, i = 0, len = static_cast<int>(strlen(s));
    while (i < len && n < max_lines) {
        while (i < len && s[i] == ' ') {
            ++i;
        }
        if (i >= len) {
            break;
        }
        int start = i, last_space = -1, w = 0, j = i;
        char one[2] = {0, 0};
        while (j < len) {
            one[0] = s[j];
            int cw = TextWidth(kFontS, one);
            if (w + cw > maxw) {
                break;
            }
            w += cw;
            if (s[j] == ' ') {
                last_space = j;
            }
            ++j;
        }
        int end = j;
        if (j < len && last_space > start) {
            end = last_space;  // ngat o khoang trang
        }
        starts[n] = start;
        lens[n] = end - start;
        ++n;
        i = end;
    }
    return n;
}

void DrawChat(const ChatView& v) {
    Clear();
    SetColor(true);

    // ---- Thanh tren: WiFi | trang thai | pin
    const char* top = (v.notif != nullptr && v.notif[0]) ? v.notif : v.status;
    if (top != nullptr && top[0]) {
        int w = TextWidth(kFontS, top);
        if (w <= 66) {
            Text(kFontS, 19, 9, top);
        } else {
            int cycle = w + 24;
            int off = static_cast<int>((v.t_ms / 40) % static_cast<uint32_t>(cycle));
            Text(kFontS, 88 - off, 9, top);
            Text(kFontS, 88 - off + cycle, 9, top);
            SetColor(false);
            Box(0, 0, 19, 12);   // che phan tran sang vung WiFi
            Box(88, 0, 40, 12);  // va vung pin
            SetColor(true);
        }
    }
    for (int b = 0; b < 4; ++b) {
        int h = 2 + b * 2;
        if (b < v.wifi) {
            Box(1 + b * 3, 9 - h + 1, 2, h);
        } else {
            Pix(1 + b * 3, 9);
            Pix(2 + b * 3, 9);
        }
    }
    if (v.wifi == 0) {
        Line(14, 3, 18, 9);
        Line(18, 3, 14, 9);
    }
    DrawBattery(127, 2, v.batt, true, v.t_ms);

    // ---- Giua: bieu cam + song am
    int emo = FaceFor(v.emotion != nullptr ? v.emotion : "");
    DrawFace(emo, 64, 30, v.t_ms, v.mode == kChatSpeaking);
    DrawWaves(64, 30, v.mode, v.t_ms);

    // ---- Duoi: chu AI noi (2 dong, tu lat trang)
    const char* msg = v.msg != nullptr ? v.msg : "";
    if (msg[0] == 0) {
        if (v.mode == kChatIdle) {
            // Wake word da tat (de du RAM): chi bam nut duoi de noi
            const char* a = "Press";
            const char* b = "to talk";
            int wa = TextWidth(kFontS, a), wb = TextWidth(kFontS, b);
            int x = (kW - (wa + 3 + 7 + 3 + wb)) / 2;
            Text(kFontS, x, 58, a);
            TriDown(x + wa + 3, 58);
            Text(kFontS, x + wa + 13, 58, b);
        } else {
            TextC(kFontS, 64, 58, "...");
        }
        return;
    }
    int starts[24], lens[24];
    int n = WrapLines(msg, 124, starts, lens, 24);
    int pages = (n + 1) / 2;
    int page = pages > 1 ? static_cast<int>((v.msg_age_ms / 2600) % static_cast<uint32_t>(pages)) : 0;
    for (int r = 0; r < 2; ++r) {
        int li = page * 2 + r;
        if (li >= n) {
            break;
        }
        char buf[64];
        int l = std::min(lens[li], 63);
        memcpy(buf, msg + starts[li], l);
        buf[l] = 0;
        int base = 54 + r * 8;
        if (r == 0 && page == 0 && v.msg_is_user) {
            Text(kFontS, 1, base, ">");
            TextC(kFontS, 64, base, buf);
        } else {
            TextC(kFontS, 64, base, buf);
        }
    }
    if (pages > 1) {  // cham bao so trang
        for (int p = 0; p < pages && p < 8; ++p) {
            Box(126, 47 + p * 3, 2, 2);
            if (p != page) {
                SetColor(false);
                Pix(126, 47 + p * 3);
                SetColor(true);
            }
        }
    }
}

}  // namespace EriUi
