#pragma once
#include <cstddef>
#include <cstdint>
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

// ---- Chat AI
enum ChatMode { kChatIdle = 0, kChatListening, kChatSpeaking, kChatBusy };
struct ChatView {
    const char* status = "";   // trang thai (giua thanh tren)
    const char* notif = "";    // thong bao tam (neu co thi thay trang thai)
    const char* emotion = "";  // ten bieu cam cua Xiaozhi: "happy", "sad", ...
    const char* msg = "";      // chu AI noi (da doi sang ASCII)
    bool msg_is_user = false;
    int wifi = 0;              // 0 = mat ket noi, 1..4 = vach song
    int mode = kChatIdle;
    uint32_t t_ms = 0;
    uint32_t msg_age_ms = 0;   // thoi gian ke tu khi chu thay doi (de lat trang)
};
void DrawChat(const ChatView& v);

// Doi chuoi UTF-8 (tieng Viet co dau) sang ASCII khong dau. Ky tu khac (CJK...) bi bo.
void FoldAscii(const char* utf8, char* out, size_t out_size);

}  // namespace EriUi
