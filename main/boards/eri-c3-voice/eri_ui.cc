#include "eri_ui.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "eri_gfx.h"
#include "eri_hud_icons.h"

using namespace EriGfx;

namespace EriUi {

// ================================================================ Menu

void DrawMenu(int sel, int ms_left, int ms_total) {
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
        TextC(kFontS, 64, 55, "10: doi   4: chon");
    } else {
        TextC(kFontS, 64, 58, "10: doi muc    4: chon");
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

void DrawRadio(const RadioView& v) {
    Clear();
    SetColor(true);

    // Dau trang
    Draw(icNote, 1, 1);
    Text(kFontS, 13, 9, "RADIO");
    char idx[16];
    snprintf(idx, sizeof(idx), "%d/%d", v.index + 1, v.count);
    TextR(kFontS, 126, 9, idx);
    Box(0, 12, kW, 1);

    // Ten dai
    Marquee(v.name, 27, v.t_ms);

    // Dong trang thai
    const char* st = v.msg;
    if (st == nullptr || st[0] == 0) {
        st = v.phase == kRadioPlaying ? "Dang phat"
             : v.phase == kRadioBuffering ? "Dang nap bo dem..."
             : v.phase == kRadioError ? "Loi"
                                      : "Dang ket noi...";
    }
    TextC(kFontS, 64, 38, st);

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
        TextC(kFontS, 64, 56, v.connected ? "Cho du lieu Waze..." : "Cho ket noi BLE...");
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

}  // namespace EriUi
