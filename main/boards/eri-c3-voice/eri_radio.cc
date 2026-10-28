#include "eri_radio.h"

#include "audio_codec.h"  // chi de lay macro XIAOZHI_I2S_PORT
#include "eri_screen.h"
#include "eri_ui.h"
#include "config.h"
#include "eri_mp3.h"
#include "eri_battery.h"
#include "eri_radio_page.h"
#include "eri_stations.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <ssid_manager.h>
#include <wifi_manager.h>

#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_lcd_panel_ops.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <esp_crt_bundle.h>
#include <esp_tls.h>

#include <errno.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#define TAG "EriRadio"

extern esp_lcd_panel_handle_t g_eri_oled_panel;  // eri_c3_voice_board.cc

namespace EriRadio {

static constexpr size_t kStreamBufBytes = 32 * 1024;  // bo dem mang
static constexpr size_t kPrebufferBytes = 12 * 1024;  // dem truoc khi phat
static constexpr int kInBufBytes = 3072;              // du chua 1 khung MP3 (toi da ~1441 byte)
static constexpr int kPcmBytes = 1152 * 2 * 2;        // 1 khung MP3 stereo 16-bit
static constexpr int kConnReadTimeoutMs = 1000;
static constexpr int kConnReadTimeoutLimit = 8;
static constexpr int kConnReadTimedOut = -2;
static constexpr int kConnCancelled = -3;
static constexpr int kSettleMs = 350;     // cho nguoi dung ngung bam nut roi moi ket noi
static constexpr int kSaveDelayMs = 2000; // ghi NVS tre, tranh dung flash luc dang phat
static constexpr int kRecvBytes = 2048;
static constexpr uint32_t kScreenOffMs = 3 * 60 * 1000;  // tat man hinh sau 3 phut khong thao tac
static constexpr int kMaxStations = 20;       // toi da trong danh sach luu tren NVS
static constexpr size_t kMaxListBytes = 3800; // NVS: chuoi toi da ~4000 byte
static constexpr size_t kMaxNameLen = 23;
static constexpr size_t kMaxUrlLen = 159;

// Tat man hinh sau kScreenOffMs khong thao tac; bam nut/web de bat lai
static std::atomic<uint32_t> g_last_act_ms{0};
static std::atomic<bool> g_asleep{false};
static uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool WakeIfAsleep() {
    bool was = g_asleep.exchange(false);
    g_last_act_ms = NowMs();
    if (was && g_eri_oled_panel != nullptr) {
        esp_lcd_panel_disp_on_off(g_eri_oled_panel, true);
    }
    return was;
}

static void SelectStation(int idx);
static void StartWeb();

static std::atomic<int> g_station{0};
static std::atomic<int> g_volume{60};
static std::atomic<uint32_t> g_gen{1};  // tang len de bao doi dai / ket noi lai
static std::atomic<uint32_t> g_ack{0};  // task phat da xac nhan `gen` nay
static StreamBufferHandle_t g_stream = nullptr;
static i2s_chan_handle_t g_tx = nullptr;
static int g_rate = 0;
static esp_timer_handle_t g_save_timer = nullptr;

// ---------------------------------------------------------------- danh sach dai
// Mac dinh lay tu eri_stations.h; neu trang web da luu danh sach thi dung ban luu tren NVS ("rlist").

struct Entry {
    std::string name;
    std::string url;
};
static std::vector<Entry> g_list;
static std::mutex g_list_mu;

static void CopyStr(char* dst, size_t cap, const char* src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static std::string Trim(std::string t) {
    size_t a = 0;
    while (a < t.size() && (t[a] == ' ' || t[a] == '\t' || t[a] == '\r')) {
        a++;
    }
    size_t b = t.size();
    while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t' || t[b - 1] == '\r')) {
        b--;
    }
    return t.substr(a, b - a);
}

// Moi dong: ten|url. Bo dong sai. Ten doi sang ASCII khong dau (font OLED).
static int ParseList(const std::string& text, std::vector<Entry>& out) {
    out.clear();
    size_t pos = 0;
    while (pos <= text.size() && static_cast<int>(out.size()) < kMaxStations) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        size_t bar = line.find('|');
        if (bar == std::string::npos) {
            continue;
        }
        std::string name = Trim(line.substr(0, bar));
        std::string url = Trim(line.substr(bar + 1));
        if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) {
            continue;
        }
        if (url.size() > kMaxUrlLen) {
            continue;
        }
        char buf[96];
        EriUi::FoldAscii(name.c_str(), buf, sizeof(buf));
        name = Trim(buf);
        if (name.size() > kMaxNameLen) {
            name.resize(kMaxNameLen);
        }
        if (name.empty()) {
            name = "Station " + std::to_string(out.size() + 1);
        }
        out.push_back({name, url});
    }
    return static_cast<int>(out.size());
}

static std::string SerializeList(const std::vector<Entry>& l) {
    std::string r;
    for (const auto& e : l) {
        r += e.name;
        r += '|';
        r += e.url;
        r += '\n';
    }
    return r;
}

static void LoadList() {
    std::vector<Entry> l;
    {
        Settings settings("eri", false);
        std::string raw = settings.GetString("rlist", "");
        if (!raw.empty()) {
            ParseList(raw, l);
        }
    }
    if (l.empty()) {
        for (const auto& st : kStations) {
            l.push_back({st.name, st.url});
        }
    }
    std::lock_guard<std::mutex> lock(g_list_mu);
    g_list = std::move(l);
}

static int Count() {
    std::lock_guard<std::mutex> lock(g_list_mu);
    return static_cast<int>(g_list.size());
}

static bool GetUrl(int idx, std::string& url) {
    std::lock_guard<std::mutex> lock(g_list_mu);
    if (idx < 0 || idx >= static_cast<int>(g_list.size())) {
        return false;
    }
    url = g_list[idx].url;
    return true;
}

static void CopyName(int idx, char* out, size_t cap) {
    std::lock_guard<std::mutex> lock(g_list_mu);
    if (idx < 0 || idx >= static_cast<int>(g_list.size())) {
        out[0] = 0;
        return;
    }
    CopyStr(out, cap, g_list[idx].name.c_str());
}

// Ten bai dang phat (ICY StreamTitle), da doi sang ASCII
static std::mutex g_title_mu;
static char g_title[96] = "";
static char g_ip[16] = "";

static void SetTitle(const char* t) {
    std::lock_guard<std::mutex> lock(g_title_mu);
    CopyStr(g_title, sizeof(g_title), t);
}

static void GetTitle(char* out, size_t cap) {
    std::lock_guard<std::mutex> lock(g_title_mu);
    CopyStr(out, cap, g_title);
}

// Ghi dai/am luong vao NVS sau khi het thao tac (ghi flash lam khung CPU, gay giat am thanh)
static void SaveSettingsCb(void*) {
    Settings settings("eri", true);
    settings.SetInt("rstation", g_station.load());
    settings.SetInt("rvol", g_volume.load());
}

static void ScheduleSave() {
    if (g_save_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = SaveSettingsCb;
        args.name = "eri_save";
        if (esp_timer_create(&args, &g_save_timer) != ESP_OK) {
            g_save_timer = nullptr;
            return;
        }
    }
    esp_timer_stop(g_save_timer);  // loi neu chua chay: bo qua
    esp_timer_start_once(g_save_timer, static_cast<uint64_t>(kSaveDelayMs) * 1000);
}

// Ngu toi da `ms`, thoat som neu doi dai/doi kenh (de bam nut khong phai cho)
static void SleepInterruptible(int ms, uint32_t gen) {
    for (int t = 0; t < ms && g_gen.load() == gen; t += 50) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void LogHeap(const char* stage) {
    ESP_LOGI(TAG, "Heap [%s]: free=%u min=%u internal=%u largest_internal=%u",
             stage, static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(esp_get_minimum_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

// ---------------------------------------------------------------- hien thi

static std::atomic<int> g_phase{EriUi::kRadioConnecting};
static std::atomic<int> g_level{0};  // 0..100, muc am luong dang phat
static char g_msg[48] = "";

// Cap nhat trang thai hien thi (UiTask ve len man hinh)
static void UiState(int phase, const char* msg) {
    snprintf(g_msg, sizeof(g_msg), "%s", msg);
    g_phase = phase;
}

static void UiTask(void*) {
    while (true) {
        if (g_asleep.load()) {
            vTaskDelay(pdMS_TO_TICKS(200));  // man hinh tat: khong ve, khong gui I2C
            continue;
        }
        if (NowMs() - g_last_act_ms.load() > kScreenOffMs) {
            g_asleep = true;
            if (g_eri_oled_panel != nullptr) {
                esp_lcd_panel_disp_on_off(g_eri_oled_panel, false);
            }
            continue;
        }
        EriUi::RadioView v;
        int count = std::max(Count(), 1);
        int idx = std::clamp(g_station.load(), 0, count - 1);
        char name[40], title[96];
        CopyName(idx, name, sizeof(name));
        GetTitle(title, sizeof(title));
        v.name = name;
        v.title = title;
        v.ip = g_ip;
        v.batt = EriBattery::Percent();
        v.msg = g_msg;
        v.index = idx;
        v.count = count;
        v.volume = g_volume.load();
        v.phase = g_phase.load();
        v.level = g_level.load();
        v.t_ms = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        EriUi::DrawRadio(v);
        EriScreen::Present();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

// ---------------------------------------------------------------- I2S

static bool InitI2s() {
    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id = XIAOZHI_I2S_PORT(0);
    chan_cfg.role = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    if (i2s_new_channel(&chan_cfg, &g_tx, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed");
        return false;
    }

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);
    std_cfg.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = AUDIO_I2S_GPIO_BCLK;
    std_cfg.gpio_cfg.ws = AUDIO_I2S_GPIO_WS;
    std_cfg.gpio_cfg.dout = AUDIO_I2S_GPIO_DOUT;
    std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(g_tx, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed");
        return false;
    }
    g_rate = 44100;
    return i2s_channel_enable(g_tx) == ESP_OK;
}

static void SetSampleRate(int rate) {
    if (rate <= 0 || rate == g_rate) {
        return;
    }
    ESP_LOGI(TAG, "Sample rate %d -> %d", g_rate, rate);
    i2s_channel_disable(g_tx);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(rate));
    i2s_channel_reconfig_std_clock(g_tx, &clk);
    i2s_channel_enable(g_tx);
    g_rate = rate;
}

static void WriteSilence() {
    static const int16_t zeros[480] = {};
    size_t written = 0;
    i2s_channel_write(g_tx, zeros, sizeof(zeros), &written, pdMS_TO_TICKS(100));
}

// ---------------------------------------------------------------- WiFi

static bool EnsureWifi() {
    if (SsidManager::GetInstance().GetSsidList().empty()) {
        UiState(EriUi::kRadioError, "No WiFi - set up in Chat AI");
        return false;
    }

    auto& wifi = WifiManager::GetInstance();
    WifiManagerConfig config;
    config.ssid_prefix = "Xiaozhi";
    config.language = Lang::CODE;
    wifi.Initialize(config);
    wifi.SetEventCallback([](WifiEvent event, const std::string& data) {
        ESP_LOGI(TAG, "wifi event %d %s", (int)event, data.c_str());
    });
    wifi.StartStation();

    UiState(EriUi::kRadioConnecting, "Connecting WiFi...");
    while (!wifi.IsConnected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable Wi-Fi power save: %s", esp_err_to_name(ps_err));
        UiState(EriUi::kRadioConnecting, "WiFi sleep failed");
    } else {
        ESP_LOGI(TAG, "Wi-Fi power save disabled for radio playback");
    }
    return true;
}

// ---------------------------------------------------------------- HTTP tho

struct Url {
    std::string host;
    int port = 80;
    std::string path = "/";
    bool https = false;
};

static bool ParseUrl(const std::string& s, Url& u) {
    size_t p = 0;
    if (s.compare(0, 7, "http://") == 0) {
        p = 7;
        u.port = 80;
        u.https = false;
    } else if (s.compare(0, 8, "https://") == 0) {
        p = 8;
        u.port = 443;
        u.https = true;
    } else {
        return false;
    }
    size_t slash = s.find('/', p);
    std::string hostport =
        slash == std::string::npos ? s.substr(p) : s.substr(p, slash - p);
    u.path = slash == std::string::npos ? "/" : s.substr(slash);
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        u.host = hostport.substr(0, colon);
        u.port = atoi(hostport.c_str() + colon + 1);
    } else {
        u.host = hostport;
    }
    return !u.host.empty() && u.port > 0;
}

// Tim gia tri header (khong phan biet hoa thuong). `name` gom ca dau hai cham.
static bool HeaderValue(const char* hdr, const char* name, std::string& out) {
    size_t name_len = strlen(name);
    const char* p = hdr;
    while ((p = strcasestr(p, name)) != nullptr) {
        if (p == hdr || *(p - 1) == '\n') {
            p += name_len;
            while (*p == ' ') {
                p++;
            }
            const char* e = p;
            while (*e && *e != '\r' && *e != '\n') {
                e++;
            }
            out.assign(p, e - p);
            return true;
        }
        p += name_len;
    }
    return false;
}

// Ket noi: socket thuong (http) hoac esp_tls (https)
struct Conn {
    int fd = -1;
    esp_tls_t* tls = nullptr;
    bool Valid() const { return fd >= 0 || tls != nullptr; }
};

static int ConnRecv(Conn& c, void* buf, size_t len, uint32_t gen) {
    if (c.tls != nullptr) {
        while (true) {
            if (g_gen.load() != gen) {
                return kConnCancelled;
            }
            int n = esp_tls_conn_read(c.tls, static_cast<char*>(buf), len);
            if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) {
                vTaskDelay(1);
                continue;
            }
            if (n == ESP_TLS_ERR_SSL_TIMEOUT) {
                return kConnReadTimedOut;
            }
            return n;
        }
    }
    int n = recv(c.fd, buf, len, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return kConnReadTimedOut;
    }
    return n;
}

static int ConnSend(Conn& c, const void* buf, size_t len, uint32_t gen) {
    if (c.tls != nullptr) {
        size_t sent = 0;
        while (sent < len) {
            if (g_gen.load() != gen) {
                return kConnCancelled;
            }
            int n = esp_tls_conn_write(c.tls, static_cast<const char*>(buf) + sent, len - sent);
            if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) {
                vTaskDelay(1);
                continue;
            }
            if (n < 0) {
                return -1;
            }
            sent += n;
        }
        return static_cast<int>(sent);
    }
    return send(c.fd, buf, len, 0);
}

static void ConnClose(Conn& c) {
    if (c.tls != nullptr) {
        esp_tls_conn_destroy(c.tls);
        c.tls = nullptr;
    }
    if (c.fd >= 0) {
        close(c.fd);
        c.fd = -1;
    }
}

static bool ConnOpen(Conn& c, const Url& u, std::string& err) {
    if (u.https) {
        esp_tls_cfg_t cfg = {};
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms = 8000;
        c.tls = esp_tls_init();
        if (c.tls == nullptr) {
            err = "TLS out of memory";
            return false;
        }
        int r = esp_tls_conn_new_sync(u.host.c_str(), u.host.size(), u.port, &cfg, c.tls);
        if (r != 1) {
            ESP_LOGW(TAG, "TLS connect failed to %s", u.host.c_str());
            esp_tls_conn_destroy(c.tls);
            c.tls = nullptr;
            err = "Loi ket noi HTTPS";
            return false;
        }
        int fd = -1;
        if (esp_tls_get_conn_sockfd(c.tls, &fd) == ESP_OK && fd >= 0) {
            struct timeval tv = {kConnReadTimeoutMs / 1000, (kConnReadTimeoutMs % 1000) * 1000};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
        LogHeap("HTTPS connected");
        return true;
    }

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    char port[8];
    snprintf(port, sizeof(port), "%d", u.port);
    if (getaddrinfo(u.host.c_str(), port, &hints, &res) != 0 || res == nullptr) {
        err = "Server not found";
        return false;
    }
    int s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
        freeaddrinfo(res);
        err = "Socket error";
        return false;
    }
    struct timeval tv = {kConnReadTimeoutMs / 1000, (kConnReadTimeoutMs % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        close(s);
        err = "Connection failed";
        return false;
    }
    freeaddrinfo(res);
    c.fd = s;
    return true;
}

// Mo ket noi va doc phan header. Tra ve true neu thanh cong.
// hdr duoc ket thuc bang NUL sau header; byte noi dung bat dau tai hdr + body_off.
static bool OpenHttp(Conn& c, const Url& u, char* hdr, size_t hdr_cap, size_t& body_off,
                     size_t& total, std::string& err, uint32_t gen) {
    if (!ConnOpen(c, u, err)) {
        return false;
    }
    if (g_gen.load() != gen) {
        ConnClose(c);
        return false;
    }

    std::string req = "GET " + u.path + " HTTP/1.0\r\nHost: " + u.host +
                      "\r\nUser-Agent: WinampMPEG/5.0\r\nAccept: */*\r\n"
                      "Icy-MetaData: 1\r\nConnection: close\r\n\r\n";
    if (ConnSend(c, req.data(), req.size(), gen) < 0) {
        ConnClose(c);
        err = "Request failed";
        return false;
    }

    total = 0;
    int read_timeouts = 0;
    while (total < hdr_cap - 1) {
        if (g_gen.load() != gen) {
            ConnClose(c);
            return false;
        }
        int n = ConnRecv(c, hdr + total, hdr_cap - 1 - total, gen);
        if (n == kConnReadTimedOut) {
            if (++read_timeouts < kConnReadTimeoutLimit) {
                continue;
            }
            ConnClose(c);
            err = "No response";
            return false;
        }
        if (n == kConnCancelled) {
            ConnClose(c);
            return false;
        }
        if (n <= 0) {
            ConnClose(c);
            err = "No response";
            return false;
        }
        read_timeouts = 0;
        total += n;
        hdr[total] = 0;
        char* end = strstr(hdr, "\r\n\r\n");
        if (end != nullptr) {
            body_off = (end - hdr) + 4;
            end[2] = 0;  // ket thuc chuoi header, giu nguyen byte noi dung
            return true;
        }
    }
    ConnClose(c);
    err = "Header too long";
    return false;
}

// ---------------------------------------------------------------- nap bo dem

struct Feeder {
    uint32_t gen;
    int metaint;    // 0 = khong co metadata chen
    int to_meta;    // so byte con lai truoc khi toi khoi metadata
    int meta_skip;  // so byte metadata con phai doc
    char meta[256];
    int meta_len;
};

// Tach StreamTitle='...'; tu khoi metadata ICY
static void ParseMeta(const char* m) {
    const char* p = strstr(m, "StreamTitle='");
    if (p == nullptr) {
        return;
    }
    p += 13;
    const char* e = strstr(p, "';");
    std::string t(p, e != nullptr ? static_cast<size_t>(e - p) : strlen(p));
    char buf[96];
    EriUi::FoldAscii(t.c_str(), buf, sizeof(buf));
    SetTitle(Trim(buf).c_str());
}

// Day du lieu vao bo dem, tach metadata ICY neu co. Tra ve false neu doi dai (gen doi).
static bool Feed(Feeder& f, const uint8_t* d, int n) {
    int i = 0;
    while (i < n) {
        if (f.meta_skip > 0) {
            int k = std::min(f.meta_skip, n - i);
            int room = static_cast<int>(sizeof(f.meta)) - 1 - f.meta_len;
            int c = std::min(k, std::max(room, 0));
            if (c > 0) {
                memcpy(f.meta + f.meta_len, d + i, c);
                f.meta_len += c;
            }
            i += k;
            f.meta_skip -= k;
            if (f.meta_skip == 0 && f.meta_len > 0) {
                f.meta[f.meta_len] = 0;
                ParseMeta(f.meta);
                f.meta_len = 0;
            }
            continue;
        }
        if (f.metaint > 0 && f.to_meta == 0) {
            f.meta_skip = d[i] * 16;
            f.meta_len = 0;
            i++;
            f.to_meta = f.metaint;
            continue;
        }
        int take = n - i;
        if (f.metaint > 0) {
            take = std::min(take, f.to_meta);
        }
        int sent = 0;
        while (sent < take) {
            if (g_gen.load() != f.gen) {
                return false;
            }
            size_t w = xStreamBufferSend(g_stream, d + i + sent, take - sent, pdMS_TO_TICKS(100));
            sent += static_cast<int>(w);
        }
        i += take;
        if (f.metaint > 0) {
            f.to_meta -= take;
        }
    }
    return true;
}

static void StreamOnce(int idx, uint32_t gen) {
    std::string url;
    if (!GetUrl(idx, url)) {
        UiState(EriUi::kRadioError, "No stations");
        SleepInterruptible(2000, gen);
        return;
    }
    SetTitle("");
    char* hdr = static_cast<char*>(malloc(2048));
    uint8_t* buf = static_cast<uint8_t*>(malloc(kRecvBytes));
    if (hdr == nullptr || buf == nullptr) {
        free(hdr);
        free(buf);
        UiState(EriUi::kRadioError, "Out of memory");
        vTaskDelay(pdMS_TO_TICKS(3000));
        return;
    }

    Conn conn;
    std::string err;
    size_t body_off = 0, total = 0;

    for (int redirect = 0; redirect < 4; ++redirect) {
        Url u;
        if (!ParseUrl(url, u)) {
            err = "Bad URL";
            break;
        }
        if (!OpenHttp(conn, u, hdr, 2048, body_off, total, err, gen)) {
            break;
        }
        int code = 0;
        if (strncmp(hdr, "HTTP/", 5) == 0) {
            code = atoi(hdr + 9);
        } else if (strncmp(hdr, "ICY ", 4) == 0) {
            code = atoi(hdr + 4);
        }
        if (code == 200) {
            break;
        }
        if (code >= 300 && code < 400) {
            std::string loc;
            bool has = HeaderValue(hdr, "location:", loc);
            ConnClose(conn);
            if (!has || loc.empty()) {
                err = "Redirect error";
                break;
            }
            if (loc[0] == '/') {
                url = std::string(u.https ? "https://" : "http://") + u.host + ":" +
                      std::to_string(u.port) + loc;
            } else {
                url = loc;
            }
            continue;
        }
        ConnClose(conn);
        err = "HTTP " + std::to_string(code);
        break;
    }

    if (conn.Valid()) {
        std::string v;
        if (HeaderValue(hdr, "transfer-encoding:", v) && strcasestr(v.c_str(), "chunked")) {
            err = "Chunked not supported";
        } else if (HeaderValue(hdr, "content-type:", v) && strcasestr(v.c_str(), "text/html")) {
            err = "Server sent a web page";
        } else if (HeaderValue(hdr, "content-type:", v) &&
                   (strcasestr(v.c_str(), "aac") || strcasestr(v.c_str(), "ogg") ||
                    strcasestr(v.c_str(), "mp4") || strcasestr(v.c_str(), "flac"))) {
            err = "MP3 only";
        }
        if (!err.empty()) {
            ConnClose(conn);
        }
    }

    if (!conn.Valid()) {
        if (err.empty()) {
            err = "Too many redirects";
        }
        ESP_LOGW(TAG, "Open stream failed: %s", err.c_str());
        if (g_gen.load() == gen) {
            UiState(EriUi::kRadioError, err.c_str());
            SleepInterruptible(3000, gen);
        }
        free(hdr);
        free(buf);
        return;
    }

    int metaint = 0;
    std::string v;
    if (HeaderValue(hdr, "icy-metaint:", v)) {
        metaint = atoi(v.c_str());
    }

    // Doi task phat xoa bo dem cu truoc khi nap du lieu moi
    for (int i = 0; i < 100 && g_ack.load() != gen; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    Feeder f = {};
    f.gen = gen;
    f.metaint = metaint;
    f.to_meta = metaint;
    bool ok = Feed(f, reinterpret_cast<uint8_t*>(hdr) + body_off, static_cast<int>(total - body_off));
    if (ok) {
        UiState(EriUi::kRadioBuffering, "");
    }
    int read_timeouts = 0;
    while (ok && g_gen.load() == gen) {
        int n = ConnRecv(conn, buf, kRecvBytes, gen);
        if (n == kConnReadTimedOut) {
            if (++read_timeouts < kConnReadTimeoutLimit) {
                continue;
            }
            ESP_LOGW(TAG, "Stream stalled for %d seconds", kConnReadTimeoutLimit);
            break;
        }
        if (n == kConnCancelled || g_gen.load() != gen) {
            break;
        }
        if (n <= 0) {
            ESP_LOGW(TAG, "Stream ended or stalled (n=%d)", n);
            break;
        }
        read_timeouts = 0;
        ok = Feed(f, buf, n);
    }

    ConnClose(conn);
    free(hdr);
    free(buf);
}

static void NetTask(void*) {
    if (!EnsureWifi()) {
        vTaskDelete(nullptr);
        return;
    }
    StartWeb();
    while (true) {
        // Doi nguoi dung ngung bam nut (bam nhanh nhieu lan thi bo qua cac dai trung gian)
        uint32_t gen = g_gen.load();
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(kSettleMs));
            uint32_t now_gen = g_gen.load();
            if (now_gen == gen) {
                break;
            }
            gen = now_gen;
        }
        int idx = g_station.load();
        UiState(EriUi::kRadioConnecting, "");
        StreamOnce(idx, gen);
        if (g_gen.load() == gen) {
            // mat ket noi: thu lai sau 2 giay
            UiState(EriUi::kRadioError, "Connection lost, retrying...");
            SleepInterruptible(2000, gen);
        }
    }
}

// ---------------------------------------------------------------- phat

static void OutputPcm(Mp3Decoder& dec, const int16_t* pcm, int bytes, int16_t* st) {
    int rate = 0, ch = 2, bits = 16;
    if (dec.GetInfo(&rate, &ch, &bits)) {
        SetSampleRate(rate);
    }
    int frames = (ch == 2) ? bytes / 4 : bytes / 2;
    if (frames > 1152) {
        frames = 1152;
    }
    int32_t vol = g_volume.load();
    int32_t gain = vol * vol;  // 0..10000, duong cong binh phuong cho de chinh
    int32_t sum_abs = 0;
    for (int i = 0; i < frames; ++i) {
        int32_t s = (ch == 2) ? (pcm[2 * i] + pcm[2 * i + 1]) / 2 : pcm[i];
        sum_abs += s < 0 ? -s : s;  // muc nhac goc, khong phu thuoc am luong
        s = s * gain / 10000;
        st[2 * i] = st[2 * i + 1] = static_cast<int16_t>(s);
    }
    if (frames > 0) {
        int raw = std::min(100, static_cast<int>(sum_abs / frames) * 100 / 4500);
        int old = g_level.load();
        g_level = std::max(raw, old * 85 / 100);  // len nhanh, xuong tu tu
    }
    size_t written = 0;
    i2s_channel_write(g_tx, st, frames * 4, &written, portMAX_DELAY);
}

static void PlayTask(void*) {
    Mp3Decoder dec;
    uint8_t* in = static_cast<uint8_t*>(malloc(kInBufBytes));
    int16_t* pcm = static_cast<int16_t*>(malloc(kPcmBytes));
    int16_t* st = static_cast<int16_t*>(malloc(kPcmBytes));
    if (in == nullptr || pcm == nullptr || st == nullptr || !dec.Open()) {
        ESP_LOGE(TAG, "Play task init failed");
        UiState(EriUi::kRadioError, "MP3 decoder error");
        vTaskDelete(nullptr);
        return;
    }
    LogHeap("decoder ready");

    int in_len = 0;
    uint32_t my_gen = 0;
    bool prebuffering = true;
    TickType_t prebuffer_start = xTaskGetTickCount();
    TickType_t heap_log_tick = prebuffer_start;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (now - heap_log_tick >= pdMS_TO_TICKS(30000)) {
            LogHeap("radio playing");
            heap_log_tick = now;
        }

        uint32_t gen = g_gen.load();
        if (gen != my_gen) {
            dec.Reset();
            in_len = 0;
            uint8_t tmp[256];
            while (xStreamBufferReceive(g_stream, tmp, sizeof(tmp), 0) > 0) {
            }
            my_gen = gen;
            g_ack.store(gen);
            prebuffering = true;
            prebuffer_start = xTaskGetTickCount();
        }

        if (prebuffering) {
            if (xStreamBufferBytesAvailable(g_stream) >= kPrebufferBytes ||
                (xTaskGetTickCount() - prebuffer_start) > pdMS_TO_TICKS(6000)) {
                prebuffering = false;
                if (g_phase.load() == EriUi::kRadioBuffering) {
                    UiState(EriUi::kRadioPlaying, "");
                }
            } else {
                WriteSilence();
                continue;
            }
        }

        size_t got = xStreamBufferReceive(g_stream, in + in_len, kInBufBytes - in_len,
                                          pdMS_TO_TICKS(50));
        in_len += static_cast<int>(got);
        if (in_len == 0) {
            WriteSilence();  // het du lieu: phat im lang thay vi lap tieng
            continue;
        }

        bool progressed = true;
        while (in_len > 0 && progressed) {
            int consumed = 0, out_bytes = 0;
            dec.Decode(in, in_len, &consumed, reinterpret_cast<uint8_t*>(pcm), kPcmBytes,
                       &out_bytes);
            if (consumed > 0) {
                memmove(in, in + consumed, in_len - consumed);
                in_len -= consumed;
            }
            if (out_bytes > 0) {
                OutputPcm(dec, pcm, out_bytes, st);
            }
            progressed = (consumed > 0 || out_bytes > 0);
            if (!progressed && in_len >= 2048) {
                // 2KB ma khong giai ma duoc -> du lieu rac, bo 1 byte de tim lai khung
                memmove(in, in + 1, in_len - 1);
                in_len--;
                progressed = true;
            }
            if (g_gen.load() != my_gen) {
                break;
            }
        }
    }
}

// ---------------------------------------------------------------- web

static void JsonEsc(const char* in, std::string& out) {
    for (; *in; ++in) {
        unsigned char c = static_cast<unsigned char>(*in);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c >= 32 && c < 127) {
            out += static_cast<char>(c);
        }
    }
}

static bool QueryInt(httpd_req_t* req, const char* key, int* out) {
    char q[48], val[12];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(q, key, val, sizeof(val)) != ESP_OK) {
        return false;
    }
    *out = atoi(val);
    return true;
}

static esp_err_t HRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kRadioPage, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t HState(httpd_req_t* req) {
    char name[40], title[96];
    int idx = g_station.load();
    CopyName(idx, name, sizeof(name));
    GetTitle(title, sizeof(title));
    int ph = g_phase.load();
    const char* p = ph == EriUi::kRadioPlaying     ? "Playing"
                    : ph == EriUi::kRadioBuffering ? "Buffering..."
                    : ph == EriUi::kRadioError     ? "Error"
                                                   : "Connecting...";
    std::string j = "{\"i\":" + std::to_string(idx) + ",\"v\":" + std::to_string(g_volume.load()) +
                    ",\"n\":\"";
    JsonEsc(name, j);
    j += "\",\"t\":\"";
    JsonEsc(title, j);
    j += "\",\"p\":\"";
    j += p;
    j += "\",\"m\":\"";
    JsonEsc(g_msg, j);
    j += "\",\"b\":" + std::to_string(EriBattery::Percent()) + ",\"mv\":" + std::to_string(EriBattery::Millivolts()) + "}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, j.c_str(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t HPlay(httpd_req_t* req) {
    WakeIfAsleep();
    int i = 0;
    if (!QueryInt(req, "i", &i) || i < 0 || i >= Count()) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad station");
    }
    SelectStation(i);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t HVol(httpd_req_t* req) {
    WakeIfAsleep();
    int v = 0;
    if (!QueryInt(req, "v", &v)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad volume");
    }
    g_volume = std::clamp(v, 0, 100);
    ScheduleSave();
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t HListGet(httpd_req_t* req) {
    std::string s;
    {
        std::lock_guard<std::mutex> lock(g_list_mu);
        s = SerializeList(g_list);
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, s.c_str(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t HListPost(httpd_req_t* req) {
    WakeIfAsleep();
    size_t len = req->content_len;
    if (len == 0 || len > 4096) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad size");
    }
    std::string body;
    body.resize(len);
    size_t got = 0;
    int retry = 0;
    while (got < len) {
        int r = httpd_req_recv(req, &body[got], len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++retry < 5) {
            continue;
        }
        if (r <= 0) {
            return ESP_FAIL;
        }
        got += r;
    }
    std::vector<Entry> nl;
    if (ParseList(body, nl) == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no valid station (need name|http://...)");
    }
    std::string ser = SerializeList(nl);
    if (ser.size() > kMaxListBytes) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "list too long");
    }
    {
        Settings settings("eri", true);
        settings.SetString("rlist", ser);
    }
    std::string old_url, new_url;
    int cur = g_station.load();
    GetUrl(cur, old_url);
    {
        std::lock_guard<std::mutex> lock(g_list_mu);
        g_list = std::move(nl);
    }
    int count = Count();
    if (cur >= count) {
        SelectStation(0);
    } else if (!GetUrl(cur, new_url) || new_url != old_url) {
        SelectStation(cur);  // dai dang phat bi doi url -> ket noi lai
    }
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t HReset(httpd_req_t* req) {
    WakeIfAsleep();
    {
        Settings settings("eri", true);
        settings.EraseKey("rlist");
    }
    LoadList();
    SelectStation(0);
    return httpd_resp_sendstr(req, "ok");
}

static void AddUri(httpd_handle_t srv, const char* uri, httpd_method_t m,
                   esp_err_t (*h)(httpd_req_t*)) {
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = m;
    u.handler = h;
    httpd_register_uri_handler(srv, &u);
}

static void StartWeb() {
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (nif != nullptr && esp_netif_get_ip_info(nif, &info) == ESP_OK) {
        esp_ip4addr_ntoa(&info.ip, g_ip, sizeof(g_ip));
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 3;  // chua socket cho luong nhac
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    cfg.task_priority = 3;  // thap hon task phat/mang
    httpd_handle_t srv = nullptr;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "Web server start failed");
        return;
    }
    AddUri(srv, "/", HTTP_GET, HRoot);
    AddUri(srv, "/api/state", HTTP_GET, HState);
    AddUri(srv, "/api/list", HTTP_GET, HListGet);
    AddUri(srv, "/api/list", HTTP_POST, HListPost);
    AddUri(srv, "/api/play", HTTP_POST, HPlay);
    AddUri(srv, "/api/vol", HTTP_POST, HVol);
    AddUri(srv, "/api/reset", HTTP_POST, HReset);
    ESP_LOGI(TAG, "Web control at http://%s/", g_ip);
}

// ---------------------------------------------------------------- giao dien

void Start() {
    LogHeap("radio start");
    EriBattery::SetLoadMv(EriBattery::kLoadRadioMv);
    g_last_act_ms = NowMs();
    EriScreen::Begin();  // dung LVGL, tu day ve bang EriGfx
    LoadList();
    {
        Settings settings("eri", false);
        int s = settings.GetInt("rstation", 0);
        g_station = (s >= 0 && s < Count()) ? s : 0;
        g_volume = std::clamp(static_cast<int>(settings.GetInt("rvol", 60)), 0, 100);
    }

    UiState(EriUi::kRadioConnecting, "Starting...");
    xTaskCreate(UiTask, "eri_ui", 4096, nullptr, 3, nullptr);

    if (!InitI2s()) {
        UiState(EriUi::kRadioError, "I2S error");
        return;
    }
    g_stream = xStreamBufferCreate(kStreamBufBytes, 1);
    if (g_stream == nullptr) {
        g_stream = xStreamBufferCreate(kStreamBufBytes / 2, 1);
    }
    if (g_stream == nullptr) {
        UiState(EriUi::kRadioError, "Out of memory");
        return;
    }
    LogHeap("stream buffer allocated");

    xTaskCreate(PlayTask, "eri_play", 8192, nullptr, 6, nullptr);
    xTaskCreate(NetTask, "eri_net", 6144, nullptr, 5, nullptr);
}

// Doi sang dai `idx` (nut bam hoac web)
static void SelectStation(int idx) {
    g_station = idx;
    g_gen++;
    ScheduleSave();
    g_level = 0;
    SetTitle("");
    UiState(EriUi::kRadioConnecting, "");
}

void NextStation() {
    int count = Count();
    if (count <= 1) {
        return;
    }
    SelectStation((g_station.load() + 1) % count);
}

void VolumeStep(int delta) {
    int v = g_volume.load() + delta;
    if (v > 100) {
        v = 10;  // vong lai
    }
    v = std::max(v, 0);
    g_volume = v;
    ScheduleSave();
}

}  // namespace EriRadio
