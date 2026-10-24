#include "eri_radio.h"

#include "audio_codec.h"  // chi de lay macro XIAOZHI_I2S_PORT
#include "board.h"
#include "config.h"
#include "display.h"
#include "eri_mp3.h"
#include "eri_stations.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <ssid_manager.h>
#include <wifi_manager.h>

#include <driver/i2s_std.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <string.h>
#include <strings.h>

#include <algorithm>
#include <atomic>
#include <string>

#define TAG "EriRadio"

namespace EriRadio {

static constexpr int kStationCount = sizeof(kStations) / sizeof(kStations[0]);
static constexpr size_t kStreamBufBytes = 24 * 1024;  // bo dem mang
static constexpr size_t kPrebufferBytes = 8 * 1024;   // dem truoc khi phat
static constexpr int kInBufBytes = 3072;              // du chua 1 khung MP3 (toi da ~1441 byte)
static constexpr int kPcmBytes = 1152 * 2 * 2;        // 1 khung MP3 stereo 16-bit

static std::atomic<int> g_station{0};
static std::atomic<int> g_volume{60};
static std::atomic<uint32_t> g_gen{1};  // tang len de bao doi dai / ket noi lai
static std::atomic<uint32_t> g_ack{0};  // task phat da xac nhan `gen` nay
static StreamBufferHandle_t g_stream = nullptr;
static i2s_chan_handle_t g_tx = nullptr;
static int g_rate = 0;

// ---------------------------------------------------------------- hien thi

static Display* Disp() { return Board::GetInstance().GetDisplay(); }

static void ShowLine(const std::string& line) {
    char status[32];
    snprintf(status, sizeof(status), "Radio %d/%d", g_station.load() + 1, kStationCount);
    Disp()->SetStatus(status);
    Disp()->SetChatMessage("system", line.c_str());
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
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
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
        ShowLine("Chua co WiFi. Vao Chat AI de cai WiFi");
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

    ShowLine("Dang ket noi WiFi...");
    while (!wifi.IsConnected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
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

// Mo ket noi va doc phan header. Tra ve socket (>=0) hoac -1.
// hdr duoc ket thuc bang NUL sau header; byte noi dung bat dau tai hdr + body_off.
static int OpenHttp(const Url& u, char* hdr, size_t hdr_cap, size_t& body_off, size_t& total,
                    std::string& err) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    char port[8];
    snprintf(port, sizeof(port), "%d", u.port);
    if (getaddrinfo(u.host.c_str(), port, &hints, &res) != 0 || res == nullptr) {
        err = "Khong tim thay may chu";
        return -1;
    }
    int s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
        freeaddrinfo(res);
        err = "Loi socket";
        return -1;
    }
    struct timeval tv = {8, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        close(s);
        err = "Khong ket noi duoc";
        return -1;
    }
    freeaddrinfo(res);

    std::string req = "GET " + u.path + " HTTP/1.0\r\nHost: " + u.host +
                      "\r\nUser-Agent: Mozilla/5.0 (ESP32)\r\nAccept: */*\r\n"
                      "Icy-MetaData: 0\r\nConnection: close\r\n\r\n";
    if (send(s, req.data(), req.size(), 0) < 0) {
        close(s);
        err = "Gui yeu cau loi";
        return -1;
    }

    total = 0;
    while (total < hdr_cap - 1) {
        int n = recv(s, hdr + total, hdr_cap - 1 - total, 0);
        if (n <= 0) {
            close(s);
            err = "Khong co phan hoi";
            return -1;
        }
        total += n;
        hdr[total] = 0;
        char* end = strstr(hdr, "\r\n\r\n");
        if (end != nullptr) {
            body_off = (end - hdr) + 4;
            end[2] = 0;  // ket thuc chuoi header, giu nguyen byte noi dung
            return s;
        }
    }
    close(s);
    err = "Header qua dai";
    return -1;
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
    uint8_t* buf = static_cast<uint8_t*>(malloc(1024));
    if (hdr == nullptr || buf == nullptr) {
        free(hdr);
        free(buf);
        ShowLine("Het bo nho");
        vTaskDelay(pdMS_TO_TICKS(3000));
        return;
    }

    int sock = -1;
    std::string err;
    size_t body_off = 0, total = 0;

    for (int redirect = 0; redirect < 4; ++redirect) {
        Url u;
        if (!ParseUrl(url, u)) {
            err = "URL sai";
            break;
        }
        if (u.https) {
            err = "HTTPS chua ho tro";
            break;
        }
        sock = OpenHttp(u, hdr, 2048, body_off, total, err);
        if (sock < 0) {
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
            close(sock);
            sock = -1;
            if (!has || loc.empty()) {
                err = "Redirect loi";
                break;
            }
            if (loc[0] == '/') {
                url = "http://" + u.host + ":" + std::to_string(u.port) + loc;
            } else {
                url = loc;
            }
            continue;
        }
        close(sock);
        sock = -1;
        err = "HTTP " + std::to_string(code);
        break;
    }

    if (sock >= 0) {
        std::string v;
        if (HeaderValue(hdr, "transfer-encoding:", v) && strcasestr(v.c_str(), "chunked")) {
            err = "Chunked chua ho tro";
        } else if (HeaderValue(hdr, "content-type:", v) &&
                   (strcasestr(v.c_str(), "aac") || strcasestr(v.c_str(), "ogg") ||
                    strcasestr(v.c_str(), "mp4") || strcasestr(v.c_str(), "flac"))) {
            err = "Chi ho tro MP3";
        }
        if (!err.empty()) {
            close(sock);
            sock = -1;
        }
    }

    if (sock < 0) {
        if (err.empty()) {
            err = "Qua nhieu redirect";
        }
        ESP_LOGW(TAG, "Open stream failed: %s", err.c_str());
        if (g_gen.load() == gen) {
            ShowLine(err);
            vTaskDelay(pdMS_TO_TICKS(3000));
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
        ShowLine(kStations[idx].name);
    }
    while (ok) {
        int n = recv(sock, buf, 1024, 0);
        if (n <= 0) {
            ESP_LOGW(TAG, "Stream ended or stalled (n=%d)", n);
            break;
        }
        ok = Feed(f, buf, n);
    }

    close(sock);
    free(hdr);
    free(buf);
}

static void NetTask(void*) {
    if (!EnsureWifi()) {
        vTaskDelete(nullptr);
        return;
    }
    while (true) {
        uint32_t gen = g_gen.load();
        int idx = g_station.load();
        ShowLine(std::string(kStations[idx].name) + " ...");
        StreamOnce(idx, gen);
        if (g_gen.load() == gen) {
            // mat ket noi: thu lai sau 2 giay
            ShowLine("Mat ket noi, thu lai...");
            vTaskDelay(pdMS_TO_TICKS(2000));
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
    for (int i = 0; i < frames; ++i) {
        int32_t s = (ch == 2) ? (pcm[2 * i] + pcm[2 * i + 1]) / 2 : pcm[i];
        s = s * gain / 10000;
        st[2 * i] = st[2 * i + 1] = static_cast<int16_t>(s);
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
        ShowLine("Loi bo giai ma MP3");
        vTaskDelete(nullptr);
        return;
    }

    int in_len = 0;
    uint32_t my_gen = 0;
    bool prebuffering = true;
    TickType_t prebuffer_start = xTaskGetTickCount();

    while (true) {
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
    {
        Settings settings("eri", false);
        int s = settings.GetInt("rstation", 0);
        g_station = (s >= 0 && s < kStationCount) ? s : 0;
        g_volume = std::clamp(settings.GetInt("rvol", 60), 0, 100);
    }

    if (!InitI2s()) {
        ShowLine("Loi I2S");
        return;
    }
    g_stream = xStreamBufferCreate(kStreamBufBytes, 1);
    if (g_stream == nullptr) {
        g_stream = xStreamBufferCreate(kStreamBufBytes / 2, 1);
    }
    if (g_stream == nullptr) {
        ShowLine("Het bo nho");
        return;
    }

    ShowLine("Khoi dong radio...");
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
    {
        Settings settings("eri", true);
        settings.SetInt("rstation", idx);
    }
    ShowLine(std::string(kStations[idx].name) + " ...");
}

void VolumeStep(int delta) {
    int v = g_volume.load() + delta;
    if (v > 100) {
        v = 10;  // vong lai
    }
    v = std::max(v, 0);
    g_volume = v;
    {
        Settings settings("eri", true);
        settings.SetInt("rvol", v);
    }
    Disp()->ShowNotification("Am luong " + std::to_string(v), 1500);
}

}  // namespace EriRadio
