#pragma once

// Cai dat nguoi dung luu trong NVS (namespace "eri"). Header-only de khong phai touch CMake.
//  - soff: tu tat man hinh Radio (0 = 1 phut, 1 = 3 phut, 2 = 10 phut, 3 = khong tat)
//  - wcity: thanh pho cho thoi tiet (chi so trong kCities)
//  - wps : tiet kiem dien WiFi cua Radio (0 = tat -> it giat, 1 = bat -> tiet kiem pin)

#include "settings.h"

#include <stdint.h>

namespace EriPrefs {

inline constexpr int kScreenOffCount = 4;
inline int g_soff = -1;
inline int g_wps = -1;
inline int g_wcity = -1;
inline int g_aoff = -1;

struct City {
    const char* name;
    float lat, lon;
};
inline constexpr City kCities[] = {
    {"Ho Chi Minh", 10.78f, 106.70f}, {"Ha Noi", 21.03f, 105.85f},   {"Da Nang", 16.05f, 108.20f},
    {"Hai Phong", 20.86f, 106.68f},   {"Can Tho", 10.03f, 105.78f},  {"Hue", 16.46f, 107.59f},
    {"Nha Trang", 12.24f, 109.19f},   {"Da Lat", 11.94f, 108.44f},   {"Vung Tau", 10.35f, 107.08f},
    {"Quy Nhon", 13.78f, 109.22f},
};
inline constexpr int kCityCount = sizeof(kCities) / sizeof(kCities[0]);

inline int CityIndex() {
    if (g_wcity < 0) {
        Settings st("eri", false);
        int v = st.GetInt("wcity", 0);
        g_wcity = (v >= 0 && v < kCityCount) ? v : 0;
    }
    return g_wcity;
}
inline void NextCity() { g_wcity = (CityIndex() + 1) % kCityCount; }

inline int ScreenOffIndex() {
    if (g_soff < 0) {
        Settings st("eri", false);
        int v = st.GetInt("soff", 1);
        g_soff = (v >= 0 && v < kScreenOffCount) ? v : 1;
    }
    return g_soff;
}

inline bool WifiPowerSave() {
    if (g_wps < 0) {
        Settings st("eri", false);
        g_wps = st.GetInt("wps", 0) != 0 ? 1 : 0;
    }
    return g_wps != 0;
}

// 0 = khong bao gio tat
inline uint32_t ScreenOffMs() {
    static const uint32_t kMs[kScreenOffCount] = {60000, 180000, 600000, 0};
    return kMs[ScreenOffIndex()];
}

inline const char* ScreenOffLabel(int idx) {
    static const char* const kL[kScreenOffCount] = {"1 min", "3 min", "10 min", "Never"};
    return kL[(idx >= 0 && idx < kScreenOffCount) ? idx : 1];
}

// Tu tat nguon (deep sleep) khi khong thao tac: 0 = khong, 1 = 10 phut, 2 = 30 phut, 3 = 60 phut
inline constexpr int kAutoOffCount = 4;
inline int AutoOffIndex() {
    if (g_aoff < 0) {
        Settings st("eri", false);
        int v = st.GetInt("aoff", 0);
        g_aoff = (v >= 0 && v < kAutoOffCount) ? v : 0;
    }
    return g_aoff;
}
inline uint32_t AutoOffMs() {
    static const uint32_t kMs[kAutoOffCount] = {0, 10 * 60000u, 30 * 60000u, 60 * 60000u};
    return kMs[AutoOffIndex()];
}
inline const char* AutoOffLabel(int idx) {
    static const char* const kL[kAutoOffCount] = {"Never", "10 min", "30 min", "60 min"};
    return kL[(idx >= 0 && idx < kAutoOffCount) ? idx : 0];
}
inline void NextAutoOff() { g_aoff = (AutoOffIndex() + 1) % kAutoOffCount; }

inline void NextScreenOff() { g_soff = (ScreenOffIndex() + 1) % kScreenOffCount; }
inline void ToggleWifiPowerSave() { g_wps = WifiPowerSave() ? 0 : 1; }

inline void Save() {
    Settings st("eri", true);
    st.SetInt("soff", ScreenOffIndex());
    st.SetInt("wps", WifiPowerSave() ? 1 : 0);
    st.SetInt("wcity", CityIndex());
    st.SetInt("aoff", AutoOffIndex());
}

}  // namespace EriPrefs
