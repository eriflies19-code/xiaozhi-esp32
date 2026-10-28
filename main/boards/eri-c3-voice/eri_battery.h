#pragma once

// Doc muc pin: cau chia ap 100k + 100k, diem giua noi GPIO0 (ADC1_CH0 tren ESP32-C3).
// Dien ap chan = Vpin / 2. Header-only de khong phai touch CMake.
//
// Gan them tu 100nF tu GPIO0 xuong GND de doc on dinh (nguon ADC tro khang 50k kha cao).
// Neu so doc lech so voi dong ho van nang: chinh kCalib (vd dong ho 3.95V ma doc 3.90V
// thi kCalib = 3.95 / 3.90 = 1.013).

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

namespace EriBattery {

inline constexpr adc_channel_t kChannel = ADC_CHANNEL_0;  // GPIO0
inline constexpr float kDivider = 2.0f;                   // 100k / 100k
inline constexpr float kCalib = 1.0f;
inline constexpr int kSamples = 32;
inline constexpr int kPeriodMs = 2000;

// Pin 300mAh co noi tro kha cao nen dien ap sut khi co tai. Bu lai theo che do de so % dong nhat:
// dien ap dung de tinh % = dien ap do duoc + kLoad*Mv. Chinh cac so nay cho khop (xem log
// "battery=...mV": so mV o menu so voi so mV o che do do, hieu so chinh la muc can bu).
inline constexpr int kLoadMenuMv = 0;
inline constexpr int kLoadRadioMv = 100;
inline constexpr int kLoadChatMv = 100;
inline constexpr int kLoadHudMv = 40;
inline constexpr int kSettleMs = 20000;  // sau khi doi che do, giu nguyen so % cu trong luc dien ap on dinh

inline std::atomic<int> g_mv{0};    // dien ap pin (mV), 0 = chua doc duoc
inline std::atomic<int> g_pct{-1};  // 0..100, -1 = chua co so lieu
inline bool g_started = false;
inline std::atomic<int> g_load_mv{0};
inline std::atomic<bool> g_load_changed{false};

// Moi ung dung goi khi bat dau (Radio/Chat/HUD) de bao muc tai
inline void SetLoadMv(int mv) {
    g_load_mv = mv;
    g_load_changed = true;
}

// Pin Li-ion/LiPo 1 cell: dien ap (mV) -> phan tram, noi suy tuyen tinh giua cac moc
inline int PercentFromMv(int mv) {
    static const int kMv[] = {3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 3950, 4000, 4100, 4200};
    static const int kPc[] = {0, 5, 10, 20, 30, 40, 52, 62, 72, 80, 92, 100};
    constexpr int n = sizeof(kMv) / sizeof(kMv[0]);
    if (mv <= kMv[0]) {
        return 0;
    }
    if (mv >= kMv[n - 1]) {
        return 100;
    }
    for (int i = 1; i < n; ++i) {
        if (mv <= kMv[i]) {
            return kPc[i - 1] + (kPc[i] - kPc[i - 1]) * (mv - kMv[i - 1]) / (kMv[i] - kMv[i - 1]);
        }
    }
    return 100;
}

inline void Task(void*) {
    adc_oneshot_unit_handle_t unit = nullptr;
    adc_oneshot_unit_init_cfg_t uc = {};
    uc.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&uc, &unit) != ESP_OK) {
        ESP_LOGE("EriBattery", "ADC init failed");
        vTaskDelete(nullptr);
        return;
    }
    adc_oneshot_chan_cfg_t cc = {};
    cc.atten = ADC_ATTEN_DB_12;  // doc toi ~3.1V (pin 4.2V / 2 = 2.1V)
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(unit, kChannel, &cc);

    adc_cali_handle_t cali = nullptr;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cf = {};
    cf.unit_id = ADC_UNIT_1;
    cf.chan = kChannel;
    cf.atten = ADC_ATTEN_DB_12;
    cf.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cf, &cali) != ESP_OK) {
        cali = nullptr;
    }
#endif

    float filtered = 0;
    int shown = -1;  // % dang hien thi (co tre de khong nhay +-1)
    TickType_t settle_until = 0;
    int log_tick = 0;
    vTaskDelay(pdMS_TO_TICKS(300));
    while (true) {
        long sum = 0;
        int n = 0;
        for (int i = 0; i < kSamples; ++i) {
            int raw = 0;
            if (adc_oneshot_read(unit, kChannel, &raw) == ESP_OK) {
                sum += raw;
                ++n;
            }
            vTaskDelay(1);
        }
        if (n > 0) {
            int raw = static_cast<int>(sum / n);
            int pin_mv = 0;
            if (cali == nullptr || adc_cali_raw_to_voltage(cali, raw, &pin_mv) != ESP_OK) {
                pin_mv = raw * 3100 / 4095;  // gan dung neu khong co hieu chuan
            }
            if (g_load_changed.exchange(false)) {
                filtered = 0;  // doc lai tu dau theo muc tai moi
                settle_until = xTaskGetTickCount() + pdMS_TO_TICKS(kSettleMs);
            }
            float bat = pin_mv * kDivider * kCalib + g_load_mv.load();
            filtered = (filtered == 0) ? bat : filtered + (bat - filtered) / 8.0f;  // lam muot (~16 giay)
            int mv = static_cast<int>(filtered);
            g_mv = mv;
            int pct = PercentFromMv(mv);
            // Chi doi so hien thi khi lech >= 2%, tranh nhay +-1% do sut ap theo tai
            bool settling = shown >= 0 && xTaskGetTickCount() < settle_until;
            if (!settling && (shown < 0 || pct - shown >= 2 || shown - pct >= 2 || pct == 0 || pct == 100)) {
                shown = pct;
            }
            g_pct = shown;
            if (log_tick++ % 15 == 0) {
                ESP_LOGI("EriBattery", "raw=%d pin=%dmV battery=%dmV (+%dmV bu tai) -> %d%%", raw, pin_mv, mv,
                         g_load_mv.load(), g_pct.load());
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
    }
}

// Goi 1 lan (tu constructor cua board)
inline void Start() {
    if (g_started) {
        return;
    }
    g_started = true;
    xTaskCreate(Task, "eri_batt", 3072, nullptr, 2, nullptr);
}

inline int Percent() { return g_pct.load(); }       // -1 neu chua doc
inline int Millivolts() { return g_mv.load(); }

}  // namespace EriBattery
