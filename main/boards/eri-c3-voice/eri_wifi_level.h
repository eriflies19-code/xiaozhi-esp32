#pragma once

// Muc song WiFi 0..4 (0 = chua ket noi). Dung chung cho Chat AI, Radio va Clock.
// Anten PCB cua ESP32-C3 trong vo thuong chi doc -65..-80 dBm du van ket noi tot,
// nen nguong thap hon so voi dien thoai: 4 vach > -60, 3 > -70, 2 > -80, con lai 1.
#include <esp_wifi.h>

namespace EriWifi {

inline int Level() {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;
    }
    int r = ap.rssi;
    return r > -60 ? 4 : r > -70 ? 3 : r > -80 ? 2 : 1;
}

}  // namespace EriWifi
