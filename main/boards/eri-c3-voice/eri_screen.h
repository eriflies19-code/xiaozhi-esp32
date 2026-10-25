#pragma once

// Ve thang len OLED bang bo dem EriGfx (menu, Radio, WAZE HUD).
// Che do Chat AI van dung LVGL cua Xiaozhi.

namespace EriScreen {

void Begin();    // dung LVGL (chi goi 1 lan, sau khi Board da tao man hinh)
void Present();  // day bo dem EriGfx len man hinh neu co thay doi

}  // namespace EriScreen
