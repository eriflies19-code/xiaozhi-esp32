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
        snprintf(b, sizeof(b), "%d%%", pct);
        TextR(kFontS, x - 3, y + 6, b);
    }
}

void DrawMenu(int sel, int ms_left, int ms_total, int batt) {
    Clear();
    SetColor(true);

    static const Bmp* const kIcons[3] = {&icChat, &icRadio, &icNav};
    static const char* const kNames[3] = {"CHAT AI", "RADIO", "WAZE"};
    const int tw = 38, th = 44, gap = 4;
    const int x0 = (kW - (3 * tw + 2 * gap)) / 2;

    for (int i = 0; i < 3; ++i) {
        int x = x0 + i * (tw + gap);
        bool on = (i == sel);
        SetColor(true);
        if (on) {
            RoundBox(x, 1, tw, th, 5);
        } else {
            RoundFrame(x, 1, tw, th, 5);
        }
        SetColor(!on);
        Draw(*kIcons[i], x + (tw - kIcons[i]->w) / 2, 6);
        TextC(kFontS, x + tw / 2, 40, kNames[i]);
    }
    SetColor(true);

    if (ms_total > 0) {
        int w = 124 * std::max(ms_left, 0) / ms_total;
        Frame(2, 59, 124, 4);
        Box(2, 59, w, 4);
        Text(kFontS, 2, 55, "10:next 4:select");
        DrawBattery(127, 49, batt, true, 0);
    } else {
        Text(kFontS, 2, 58, "10:next 4:select");
        DrawBattery(127, 52, batt, true, 0);
    }
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
        TextC(kFontS, 64, 58, v.mode == kChatIdle ? "Say 'Hi,ESP' or press 4" : "...");
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
