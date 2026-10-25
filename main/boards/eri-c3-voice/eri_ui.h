#pragma once
// Cac man hinh (menu, radio, WAZE HUD) ve bang eri_gfx. Khong phu thuoc ESP-IDF.

#include <stdint.h>

namespace EriUi {

// ---- Menu chon che do. sel: 0 Chat AI, 1 Radio, 2 WAZE HUD.
// ms_total = 0: dang cho (khong dem nguoc); nguoc lai ve thanh dem nguoc.
void DrawMenu(int sel, int ms_left, int ms_total);

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
    uint32_t t_ms = 0;
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
    uint32_t t_ms = 0;
};
void DrawHud(const HudView& v);

}  // namespace EriUi
