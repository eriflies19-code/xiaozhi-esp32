#pragma once

// App "CLOCK": dong ho lon + thoi tiet (Open-Meteo) + bao thuc.
// Nut tren = GPIO10, nut duoi = GPIO4 (xem tung trang).
namespace EriClockApp {

void Start();       // khoi dong (WiFi + giao dien + am thanh), tra ve ngay
void OnDown();      // GPIO4: sang trang ke tiep / truong ke tiep / tat chuong
void OnUp();        // GPIO10: lam moi thoi tiet / bat tat bao thuc / tang gia tri / tam nghi
void OnUpHold();    // giu GPIO10: vao hoac tang nhanh khi sua bao thuc

}  // namespace EriClockApp
