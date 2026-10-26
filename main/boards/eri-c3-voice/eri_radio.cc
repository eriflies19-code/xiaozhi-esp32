#include "eri_radio.h"

#include "audio_codec.h"  // chi de lay macro XIAOZHI_I2S_PORT
#include "eri_screen.h"
#include "eri_ui.h"
#include "config.h"
#include "eri_mp3.h"
#include "eri_stations.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <ssid_manager.h>
#include <wifi_manager.h>

#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
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
#include <string>

#define TAG "EriRadio"

namespace EriRadio {

static constexpr int kStationCount = sizeof(kStations) / sizeof(kStations[0]);
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

static std::atomic<int> g_station{0};
static std::atomic<int> g_volume{60};
static std::atomic<uint32_t> g_gen{1};  // tang len de bao doi dai / ket noi lai
static std::atomic<uint32_t> g_ack{0};  // task phat da xac nhan `gen` nay
static StreamBufferHandle_t g_stream = nullptr;
static i2s_chan_handle_t g_tx = nullptr;
static int g_rate = 0;
static esp_timer_handle_t g_save_timer = nullptr;

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
        EriUi::RadioView v;
        int idx = std::clamp(g_station.load(), 0, kStationCount - 1);
        v.name = kStations[idx].name;
        v.msg = g_msg;
        v.index = idx;
        v.count = kStationCount;
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
                      "Icy-MetaData: 0\r\nConnection: close\r\n\r\n";
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
    int metaint;   // 0 = khong co metadata chen
    int to_meta;   // so byte con lai truoc khi toi khoi metadata
    int meta_skip; // so byte metadata can bo qua
};

// Day du lieu vao bo dem, bo metadata ICY neu co. Tra ve false neu doi dai (gen doi).
static bool Feed(Feeder& f, const uint8_t* d, int n) {
    int i = 0;
    while (i < n) {
        if (f.meta_skip > 0) {
            int k = std::min(f.meta_skip, n - i);
            i += k;
            f.meta_skip -= k;
            continue;
        }
        if (f.metaint > 0 && f.to_meta == 0) {
            f.meta_skip = d[i] * 16;
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
    std::string url = kStations[idx].url;
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

    Feeder f{gen, metaint, metaint, 0};
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

// ---------------------------------------------------------------- giao dien

void Start() {
    LogHeap("radio start");
    EriScreen::Begin();  // dung LVGL, tu day ve bang EriGfx
    {
        Settings settings("eri", false);
        int s = settings.GetInt("rstation", 0);
        g_station = (s >= 0 && s < kStationCount) ? s : 0;
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

void NextStation() {
    if (kStationCount <= 1) {
        return;
    }
    int idx = (g_station.load() + 1) % kStationCount;
    g_station = idx;
    g_gen++;
    ScheduleSave();
    g_level = 0;
    UiState(EriUi::kRadioConnecting, "");
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
