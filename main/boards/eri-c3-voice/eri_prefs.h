#pragma once

// Cai dat nguoi dung luu trong NVS (namespace "eri"). Header-only de khong phai touch CMake.
//  - soff: tu tat man hinh Radio (0 = 1 phut, 1 = 3 phut, 2 = 10 phut, 3 = khong tat)
//  - wps : tiet kiem dien WiFi cua Radio (0 = tat -> it giat, 1 = bat -> tiet kiem pin)

#include "settings.h"

#include <stdint.h>

namespace EriPrefs {

inline constexpr int kScreenOffCount = 4;
inline int g_soff = -1;
inline int g_wps = -1;

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

inline void NextScreenOff() { g_soff = (ScreenOffIndex() + 1) % kScreenOffCount; }
inline void ToggleWifiPowerSave() { g_wps = WifiPowerSave() ? 0 : 1; }

inline void Save() {
    Settings st("eri", true);
    st.SetInt("soff", ScreenOffIndex());
    st.SetInt("wps", WifiPowerSave() ? 1 : 0);
}

}  // namespace EriPrefs
