#pragma once

// Che do WAZE HUD: nhan du lieu HLP/1 tu WazeMod qua BLE (NimBLE), ve len OLED.
// Chay rieng, khong dung WiFi va khong khoi dong Application.

namespace EriHud {

void Start();        // goi sau khi chon WAZE HUD trong menu
void ToggleMute();   // GPIO4 bam: tat/bat tieng bip
void ToggleDemo();   // GPIO10 bam: bat/tat du lieu gia de thu man hinh

}  // namespace EriHud
