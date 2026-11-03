#include "eri_clock_app.h"

#include "config.h"
#include "eri_battery.h"
#include "eri_wifi_level.h"
#include "eri_clock.h"
#include "eri_prefs.h"
#include "eri_screen.h"
#include "eri_ui.h"
#include "assets/lang_config.h"

#include <ssid_manager.h>
#include <wifi_manager.h>

#include "audio_codec.h"  // XIAOZHI_I2S_PORT, AUDIO_CODEC_DMA_*
#include "settings.h"

#include <cJSON.h>
#include <driver/i2s_std.h>
#include <esp_crt_bundle.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_tls.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <math.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TAG "EriClockApp"

extern esp_lcd_panel_handle_t g_eri_oled_panel;  // do board tao

namespace EriClockApp {

static constexpr int kRefreshMs = 15 * 60 * 1000;  // cap nhat thoi tiet moi 15 phut
static constexpr int kRetryMs = 60 * 1000;

struct Weather {
    bool valid = false;
    int temp = 0, tmin = 0, tmax = 0, hum = 0, wind = 0, rain = 0, code = 0;
    uint32_t at_ms = 0;
    int city = -1;  // thanh pho cua so lieu nay
};

static std::mutex g_mu;
static Weather g_w;
static char g_msg[40] = "Connecting WiFi...";
static std::atomic<int> g_page{0};  // 0 dong ho, 1 thoi tiet, 2 bao thuc
static std::atomic<bool> g_refresh{true};
static std::atomic<bool> g_asleep{false};         // man hinh dang tat (bao thuc van chay)
static std::atomic<uint32_t> g_last_act_ms{0};

static uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// ---------------------------------------------------------------- Bao thuc
static constexpr int kPages = 3;                  // 0 dong ho, 1 thoi tiet, 2 bao thuc
static constexpr uint32_t kSnoozeMs = 5 * 60 * 1000;
static constexpr uint32_t kRingMaxMs = 90 * 1000;  // tu tat sau 90 giay neu khong ai bam
static constexpr int kSampleRate = 16000;
static constexpr float kAlarmVol = 0.35f;          // 0.0 - 1.0

static std::atomic<int> g_al_h{6};
static std::atomic<int> g_al_m{30};
static std::atomic<int> g_al_rep{0};   // 0 mot lan, 1 hang ngay, 2 thu 2 - thu 6
static std::atomic<bool> g_al_on{false};
static std::atomic<int> g_edit{-1};    // -1 khong sua, 0 gio, 1 phut, 2 lap lai
static std::atomic<bool> g_ringing{false};
static std::atomic<uint32_t> g_ring_start{0};
static std::atomic<uint32_t> g_snooze_until{0};  // 0 = khong tam nghi
static std::atomic<int> g_fired_key{-1};         // phut da reo (tranh reo lai trong cung 1 phut)

static i2s_chan_handle_t g_tx = nullptr;
static int16_t g_sine[256];

static void LoadAlarm() {
    Settings st("eri", false);
    g_al_h = std::clamp(static_cast<int>(st.GetInt("alh", 6)), 0, 23);
    g_al_m = std::clamp(static_cast<int>(st.GetInt("alm", 30)), 0, 59);
    g_al_rep = std::clamp(static_cast<int>(st.GetInt("alr", 0)), 0, 2);
    g_al_on = st.GetInt("alon", 0) != 0;
}

static void SaveAlarm() {
    Settings st("eri", true);
    st.SetInt("alh", g_al_h.load());
    st.SetInt("alm", g_al_m.load());
    st.SetInt("alr", g_al_rep.load());
    st.SetInt("alon", g_al_on.load() ? 1 : 0);
}

static bool InitI2s() {
    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id = XIAOZHI_I2S_PORT(0);
    chan_cfg.role = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    if (i2s_new_channel(&chan_cfg, &g_tx, nullptr) != ESP_OK) {
        return false;
    }
    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(kSampleRate));
    std_cfg.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = AUDIO_I2S_GPIO_BCLK;
    std_cfg.gpio_cfg.ws = AUDIO_I2S_GPIO_WS;
    std_cfg.gpio_cfg.dout = AUDIO_I2S_GPIO_DOUT;
    std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(g_tx, &std_cfg) != ESP_OK) {
        return false;
    }
    return i2s_channel_enable(g_tx) == ESP_OK;
}

// f = 0: im lang. Kiem tra g_ringing de tat ngay khi bam nut.
static void PlayNote(uint16_t f, uint16_t ms) {
    static int16_t buf[2 * 160];
    static uint32_t phase = 0;
    const int total = (kSampleRate / 1000) * ms;
    const int ramp = kSampleRate / 200;
    const uint32_t inc = f ? static_cast<uint32_t>((static_cast<uint64_t>(f) << 32) / kSampleRate) : 0;
    int done = 0;
    while (done < total) {
        int n = std::min(total - done, 160);
        for (int i = 0; i < n; ++i) {
            int pos = done + i, env = 256;
            if (pos < ramp) {
                env = 256 * pos / ramp;
            } else if (total - pos < ramp) {
                env = 256 * (total - pos) / ramp;
            }
            int16_t v = f ? static_cast<int16_t>((g_sine[phase >> 24] * env) >> 8) : 0;
            phase += inc;
            buf[2 * i] = v;
            buf[2 * i + 1] = v;
        }
        size_t w = 0;
        i2s_channel_write(g_tx, buf, n * 4, &w, portMAX_DELAY);
        done += n;
    }
}

// Tieng "bip-bip-bip ... bip-bip-bip" cho den khi tat
static void AudioTask(void*) {
    while (true) {
        if (!g_ringing.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        for (int i = 0; i < 4 && g_ringing.load(); ++i) {
            PlayNote(2000, 110);
            PlayNote(0, 70);
        }
        PlayNote(0, 600);
    }
}

// Bat lai man hinh. Tra ve true neu truoc do dang tat (lan bam dau chi de danh thuc).
static bool WakeIfAsleep() {
    bool was = g_asleep.exchange(false);
    g_last_act_ms = NowMs();
    if (was && g_eri_oled_panel != nullptr) {
        esp_lcd_panel_disp_on_off(g_eri_oled_panel, true);
    }
    return was;
}

static void StopRinging() {
    g_ringing = false;
}

// Kiem tra moi ~250ms: den gio thi bat dau reo; het gio tam nghi thi reo lai; qua 90s thi tu tat
static void AlarmTick() {
    uint32_t now = NowMs();
    if (g_ringing.load()) {
        if (now - g_ring_start.load() > kRingMaxMs) {
            StopRinging();
        }
        return;
    }
    uint32_t sn = g_snooze_until.load();
    if (sn != 0 && static_cast<int32_t>(now - sn) >= 0) {
        g_snooze_until = 0;
        g_ring_start = now;
        g_ringing = true;
        return;
    }
    if (!g_al_on.load() || !EriClock::Valid() || g_edit.load() >= 0) {
        return;
    }
    time_t tt = time(nullptr);
    struct tm t;
    localtime_r(&tt, &t);
    if (t.tm_hour != g_al_h.load() || t.tm_min != g_al_m.load()) {
        return;
    }
    int rep = g_al_rep.load();
    if (rep == 2 && (t.tm_wday == 0 || t.tm_wday == 6)) {
        return;
    }
    int key = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
    if (g_fired_key.exchange(key) == key) {
        return;
    }
    ESP_LOGI(TAG, "Alarm ringing %02d:%02d", t.tm_hour, t.tm_min);
    g_ring_start = now;
    g_ringing = true;
    if (rep == 0) {
        g_al_on = false;  // bao thuc 1 lan: tu tat va luu
        SaveAlarm();
    }
}


static void SetMsg(const char* m) {
    std::lock_guard<std::mutex> lk(g_mu);
    snprintf(g_msg, sizeof(g_msg), "%s", m);
}

static int WifiLevel() { return EriWifi::Level(); }

static bool EnsureWifi() {
    if (SsidManager::GetInstance().GetSsidList().empty()) {
        SetMsg("No WiFi - set up in Chat AI");
        return false;
    }
    auto& wifi = WifiManager::GetInstance();
    WifiManagerConfig config;
    config.ssid_prefix = "Xiaozhi";
    config.language = Lang::CODE;
    wifi.Initialize(config);
    wifi.StartStation();
    while (!wifi.IsConnected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // dong ho khong can toc do: tiet kiem pin
    EriClock::Init();
    EriClock::StartSntp();
    return true;
}

// GET https://api.open-meteo.com/... (HTTP/1.0 de khong bi chunked)
static bool FetchWeather(int city_idx, Weather* out) {
    const EriPrefs::City& c = EriPrefs::kCities[city_idx];
    char req[420];
    snprintf(req, sizeof(req),
             "GET /v1/forecast?latitude=%.2f&longitude=%.2f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
             "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&forecast_days=1&timezone=auto HTTP/1.0\r\n"
             "Host: api.open-meteo.com\r\nUser-Agent: eri-c3\r\nConnection: close\r\n\r\n",
             c.lat, c.lon);

    esp_tls_cfg_t cfg = {};
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    esp_tls_t* tls = esp_tls_init();
    if (tls == nullptr) {
        return false;
    }
    const char* host = "api.open-meteo.com";
    if (esp_tls_conn_new_sync(host, strlen(host), 443, &cfg, tls) != 1) {
        esp_tls_conn_destroy(tls);
        ESP_LOGW(TAG, "Khong ket noi duoc may chu thoi tiet");
        return false;
    }
    size_t len = strlen(req), sent = 0;
    while (sent < len) {
        int n = esp_tls_conn_write(tls, req + sent, len - sent);
        if (n <= 0) {
            esp_tls_conn_destroy(tls);
            return false;
        }
        sent += n;
    }
    const size_t kMax = 3072;
    char* buf = static_cast<char*>(malloc(kMax + 1));
    if (buf == nullptr) {
        esp_tls_conn_destroy(tls);
        return false;
    }
    size_t total = 0;
    while (total < kMax) {
        int n = esp_tls_conn_read(tls, buf + total, kMax - total);
        if (n <= 0) {
            break;
        }
        total += n;
    }
    esp_tls_conn_destroy(tls);
    buf[total] = 0;

    bool ok = false;
    char* body = strstr(buf, "\r\n\r\n");
    if (body != nullptr && strstr(buf, " 200") != nullptr && strstr(buf, " 200") < body) {
        body += 4;
        cJSON* doc = cJSON_Parse(body);
        if (doc != nullptr) {
            cJSON* cur = cJSON_GetObjectItem(doc, "current");
            cJSON* day = cJSON_GetObjectItem(doc, "daily");
            auto num = [](cJSON* o, const char* k, double d) {
                cJSON* it = cJSON_GetObjectItem(o, k);
                return cJSON_IsNumber(it) ? it->valuedouble : d;
            };
            auto first = [](cJSON* o, const char* k, double d) {
                cJSON* a = cJSON_GetObjectItem(o, k);
                cJSON* it = cJSON_IsArray(a) ? cJSON_GetArrayItem(a, 0) : nullptr;
                return cJSON_IsNumber(it) ? it->valuedouble : d;
            };
            if (cJSON_IsObject(cur)) {
                auto r = [](double v) { return static_cast<int>(v < 0 ? v - 0.5 : v + 0.5); };
                out->temp = r(num(cur, "temperature_2m", 0));
                out->hum = r(num(cur, "relative_humidity_2m", 0));
                out->code = r(num(cur, "weather_code", 0));
                out->wind = r(num(cur, "wind_speed_10m", 0));
                if (cJSON_IsObject(day)) {
                    out->tmax = r(first(day, "temperature_2m_max", out->temp));
                    out->tmin = r(first(day, "temperature_2m_min", out->temp));
                    out->rain = r(first(day, "precipitation_probability_max", 0));
                } else {
                    out->tmax = out->tmin = out->temp;
                }
                ok = true;
            }
            cJSON_Delete(doc);
        }
    }
    free(buf);
    return ok;
}

static void NetTask(void*) {
    if (!EnsureWifi()) {
        vTaskDelete(nullptr);
        return;
    }
    SetMsg("Loading weather...");
    uint32_t next_ms = 0;
    bool first = true;
    while (true) {
        int city = EriPrefs::CityIndex();
        bool city_changed;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            city_changed = g_w.city != city;
        }
        bool due = first || g_refresh.exchange(false) || city_changed ||
                   static_cast<int32_t>(NowMs() - next_ms) >= 0;
        if (due) {
            first = false;
            Weather w;
            if (FetchWeather(city, &w)) {
                w.valid = true;
                w.at_ms = NowMs();
                w.city = city;
                std::lock_guard<std::mutex> lk(g_mu);
                g_w = w;
                next_ms = NowMs() + kRefreshMs;
                ESP_LOGI(TAG, "Weather %s: %dC code=%d", EriPrefs::kCities[city].name, w.temp, w.code);
            } else {
                SetMsg("Weather error");
                next_ms = NowMs() + kRetryMs;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void UiTask(void*) {
    int wifi = 0, wifi_tick = 0;
    while (true) {
        if (wifi_tick-- <= 0) {
            wifi = WifiLevel();
            wifi_tick = 10;
        }
        int batt = EriBattery::Percent();
        AlarmTick();
        if (g_ringing.load() && g_asleep.load()) {
            WakeIfAsleep();  // den gio bao thuc: bat man hinh
        }
        uint32_t off_ms = EriPrefs::ScreenOffMs();
        if (!g_asleep.load() && off_ms != 0 && !g_ringing.load() && g_edit.load() < 0 &&
            NowMs() - g_last_act_ms.load() > off_ms) {
            g_asleep = true;  // tat man hinh de tiet kiem pin, bao thuc van reo
            if (g_eri_oled_panel != nullptr) {
                esp_lcd_panel_disp_on_off(g_eri_oled_panel, false);
            }
        }
        if (g_asleep.load()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (g_ringing.load()) {
            EriUi::DrawAlarmRing(g_al_h.load(), g_al_m.load(), NowMs() - g_ring_start.load());
            EriScreen::Present();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int page = g_page.load();
        if (page == 2) {
            EriUi::AlarmView v;
            v.h = g_al_h.load();
            v.m = g_al_m.load();
            v.repeat = g_al_rep.load();
            v.on = g_al_on.load();
            v.edit = g_edit.load();
            v.wifi = wifi;
            v.batt = batt;
            EriUi::DrawAlarm(v);
        } else if (page == 0) {
            EriUi::ClockView v;
            v.wifi = wifi;
            v.batt = batt;
            v.alarm_on = g_al_on.load();
            v.alarm_h = g_al_h.load();
            v.alarm_m = g_al_m.load();
            if (EriClock::Valid()) {
                time_t now = time(nullptr);
                struct tm t;
                localtime_r(&now, &t);
                v.valid = true;
                v.h = t.tm_hour;
                v.m = t.tm_min;
                v.s = t.tm_sec;
                v.day = t.tm_mday;
                v.mon = t.tm_mon + 1;
                v.year = t.tm_year + 1900;
                v.wday = t.tm_wday;
            }
            EriUi::DrawClock(v);
        } else {
            EriUi::WeatherView v;
            Weather w;
            char msg[40];
            {
                std::lock_guard<std::mutex> lk(g_mu);
                w = g_w;
                snprintf(msg, sizeof(msg), "%s", g_msg);
            }
            int city = EriPrefs::CityIndex();
            v.city = EriPrefs::kCities[city].name;
            v.msg = msg;
            v.wifi = wifi;
            v.batt = batt;
            if (w.valid && w.city == city) {
                v.valid = true;
                v.temp = w.temp;
                v.tmin = w.tmin;
                v.tmax = w.tmax;
                v.hum = w.hum;
                v.wind = w.wind;
                v.rain = w.rain;
                v.code = w.code;
                v.age_min = static_cast<int>((NowMs() - w.at_ms) / 60000);
            }
            EriUi::DrawWeather(v);
        }
        EriScreen::Present();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void Start() {
    EriBattery::SetLoadMv(EriBattery::kLoadHudMv);
    EriScreen::Begin();
    LoadAlarm();
    g_last_act_ms = NowMs();
    for (int i = 0; i < 256; ++i) {
        g_sine[i] = static_cast<int16_t>(sinf(2.0f * 3.14159265f * i / 256.0f) * 32767.0f * kAlarmVol);
    }
    if (InitI2s()) {
        xTaskCreate(AudioTask, "eri_alarm", 3072, nullptr, 3, nullptr);
    } else {
        ESP_LOGW(TAG, "Khong co am thanh: bao thuc se chi hien chu");
    }
    xTaskCreate(UiTask, "eri_ui", 4096, nullptr, 3, nullptr);
    xTaskCreate(NetTask, "eri_wx", 8192, nullptr, 4, nullptr);
}

// GPIO4 (nut duoi)
void OnDown() {
    if (!g_ringing.load() && WakeIfAsleep()) {
        return;
    }
    g_last_act_ms = NowMs();
    if (g_ringing.load()) {
        StopRinging();  // tat han
        g_snooze_until = 0;
        return;
    }
    int e = g_edit.load();
    if (e >= 0) {
        if (e >= 2) {
            g_edit = -1;  // xong: luu
            SaveAlarm();
        } else {
            g_edit = e + 1;
        }
        return;
    }
    g_page = (g_page.load() + 1) % kPages;
}

// GPIO10 (nut tren)
void OnUp() {
    if (!g_ringing.load() && WakeIfAsleep()) {
        return;
    }
    g_last_act_ms = NowMs();
    if (g_ringing.load()) {
        StopRinging();  // tam nghi 5 phut
        g_snooze_until = NowMs() + kSnoozeMs;
        return;
    }
    int e = g_edit.load();
    if (e == 0) {
        g_al_h = (g_al_h.load() + 1) % 24;
    } else if (e == 1) {
        g_al_m = (g_al_m.load() / 5 * 5 + 5) % 60;  // bam: +5 phut
    } else if (e == 2) {
        g_al_rep = (g_al_rep.load() + 1) % 3;
    } else if (g_page.load() == 2) {
        bool on = !g_al_on.load();
        g_al_on = on;
        g_fired_key = -1;
        g_snooze_until = 0;
        SaveAlarm();
    } else if (g_page.load() == 1) {
        g_refresh = true;
        SetMsg("Loading weather...");
    }
}

// Giu GPIO10: o trang bao thuc -> vao che do sua; dang sua phut -> +1 phut
void OnUpHold() {
    if (g_ringing.load() || WakeIfAsleep()) {
        return;
    }
    g_last_act_ms = NowMs();
    int e = g_edit.load();
    if (e == 1) {
        g_al_m = (g_al_m.load() + 1) % 60;
    } else if (e >= 0) {
        OnUp();
    } else if (g_page.load() == 2) {
        g_edit = 0;
        g_snooze_until = 0;
    }
}

}  // namespace EriClockApp
