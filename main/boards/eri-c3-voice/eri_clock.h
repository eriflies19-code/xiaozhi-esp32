#pragma once

// Dong ho cho menu. ESP32 khong co pin dong ho, nen gio duoc lay tu mang (SNTP) khi Radio ket noi
// WiFi (hoac tu trinh duyet khi mo trang web Radio), Chat AI cung tu cap nhat gio qua OTA.
//
// QUY UOC: dong ho he thong chua GIO DIA PHUONG (da cong mui gio), TZ = UTC. Day la cach firmware
// Xiaozhi dat gio khi cap nhat qua OTA, nen cac nguon gio khop nhau. Gio SNTP (UTC) duoc cong them
// kUtcOffsetSec ngay sau moi lan dong bo.
// Gio he thong thuong duoc giu qua cac lan khoi dong lai mem (esp_restart) nen menu van hien
// dung gio; mat nguon hoan toan thi phai ket noi WiFi lai de lay gio.

#include <esp_netif_sntp.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

namespace EriClock {

inline constexpr long kUtcOffsetSec = 7 * 3600;  // Viet Nam, UTC+7
inline bool g_sntp_started = false;

inline void Init() {
    setenv("TZ", "UTC0", 1);  // gio he thong da la gio dia phuong, khong cong them mui gio
    tzset();
}

// Gio hop le khi sau 01/01/2025 (gio mac dinh luc moi cap nguon la nam 1970)
inline bool Valid() { return time(nullptr) > 1735689600; }

// Ghi "HH:MM  dd/mm" vao out (nen dung bo dem 64 byte), hoac "--:--" neu chua co gio
inline void Format(char* out, size_t n) {
    if (!Valid()) {
        snprintf(out, n, "--:--");
        return;
    }
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(out, n, "%02d:%02d  %02d/%02d", t.tm_hour % 100, t.tm_min % 100, t.tm_mday % 100,
             (t.tm_mon + 1) % 100);
}

// Sau moi lan SNTP dat gio UTC: cong mui gio de ra gio dia phuong
inline void OnSntpSync(struct timeval*) {
    struct timeval now;
    gettimeofday(&now, nullptr);
    now.tv_sec += kUtcOffsetSec;
    settimeofday(&now, nullptr);
}

// Goi sau khi WiFi da ket noi
inline void StartSntp() {
    if (g_sntp_started) {
        return;
    }
    g_sntp_started = true;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = OnSntpSync;
    esp_netif_sntp_init(&cfg);
}

inline void SetEpoch(long epoch) {
    struct timeval tv = {static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
}

}  // namespace EriClock
