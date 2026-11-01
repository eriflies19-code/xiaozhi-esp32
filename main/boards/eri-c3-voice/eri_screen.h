#pragma once

// Ve thang len OLED bang bo dem EriGfx (menu, Radio, WAZE HUD).
// Che do Chat AI van dung LVGL cua Xiaozhi.

namespace EriScreen {

void Begin();    // dung LVGL (chi goi 1 lan, sau khi Board da tao man hinh)
void Present();  // day bo dem EriGfx len man hinh neu co thay doi

// Do sang: 5 muc (0..4), luu NVS "bright", mac dinh muc 2.
constexpr int kBrightLevels = 5;
int BrightLevel();
void SetBrightLevel(int l);  // dat + gui ngay xuong OLED (chua luu)
void SaveBrightLevel();
void ApplyBrightness();      // gui muc hien tai xuong OLED (ca Chat AI)

}  // namespace EriScreen
