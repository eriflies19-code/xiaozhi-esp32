#pragma once

namespace EriRadio {

// Khoi dong radio: tao I2S, bo dem, va 2 task (mang + phat). Tra ve ngay.
// Yeu cau: WiFi da duoc cai san (qua che do Chat AI) de dung chung SSID.
void Start();

// Goi khi co thao tac (nut bam/web). Neu man hinh dang tat thi bat lai va tra ve true
// (thao tac dau tien chi de danh thuc, khong doi dai/am luong).
bool WakeIfAsleep();

void NextStation();
void VolumeStep(int delta);  // +10 / -10, am luong 0..100

}  // namespace EriRadio
