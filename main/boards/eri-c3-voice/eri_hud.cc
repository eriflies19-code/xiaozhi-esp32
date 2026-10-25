#include "eri_hud.h"
#include "eri_hud_icons.h"

#include "audio_codec.h"  // chi de lay macro XIAOZHI_I2S_PORT va AUDIO_CODEC_DMA_*
#include "config.h"

#include <cJSON.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>

#define TAG "EriHud"

// Tay cam OLED do board tao (eri_c3_voice_board.cc)
extern esp_lcd_panel_handle_t g_eri_oled_panel;

namespace EriHud {

// ================================================================ khung hinh 1-bit
// Bo dem theo trang (page) cua SSD1306: byte = (y/8)*128 + x, bit = y%8.

static constexpr int kW = 128;
static constexpr int kH = 64;
static uint8_t g_fb[kW * kH / 8];
static uint8_t g_sent[kW * kH / 8];
static bool g_color = true;  // true = sang, false = xoa

static inline void Pix(int x, int y) {
    if (static_cast<unsigned>(x) >= kW || static_cast<unsigned>(y) >= kH) {
        return;
    }
    uint8_t& b = g_fb[(y >> 3) * kW + x];
    if (g_color) {
        b |= static_cast<uint8_t>(1u << (y & 7));
    } else {
        b &= static_cast<uint8_t>(~(1u << (y & 7)));
    }
}

static void Box(int x, int y, int w, int h) {
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            Pix(x + i, y + j);
        }
    }
}

static void Disc(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r + r) {
                Pix(cx + dx, cy + dy);
            }
        }
    }
}

// Vanh tron do day `t` pixel, ban kinh ngoai r
static void Ring(int cx, int cy, int r, int t) {
    int ri = r - t;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r + r && d2 > ri * ri + ri) {
                Pix(cx + dx, cy + dy);
            }
        }
    }
}

static void Frame(int x, int y, int w, int h) {
    Box(x, y, w, 1);
    Box(x, y + h - 1, w, 1);
    Box(x, y, 1, h);
    Box(x + w - 1, y, 1, h);
}

static int Edge(int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void Tri(int x1, int y1, int x2, int y2, int x3, int y3) {
    int minx = std::min({x1, x2, x3}), maxx = std::max({x1, x2, x3});
    int miny = std::min({y1, y2, y3}), maxy = std::max({y1, y2, y3});
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            int w0 = Edge(x2, y2, x3, y3, x, y);
            int w1 = Edge(x3, y3, x1, y1, x, y);
            int w2 = Edge(x1, y1, x2, y2, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                Pix(x, y);
            }
        }
    }
}

// Ve icon XBM 24x24 phong/thu thanh size x size (nearest neighbour)
static void Icon(const uint8_t* src, int x, int y, int size) {
    for (int dy = 0; dy < size; ++dy) {
        int sy = dy * 24 / size;
        for (int dx = 0; dx < size; ++dx) {
            int sx = dx * 24 / size;
            if (src[sy * 3 + sx / 8] & (1u << (sx & 7))) {
                Pix(x + dx, y + dy);
            }
        }
    }
}

// ---------------------------------------------------------------- font 5x7 (cot, bit0 = tren)
struct Glyph {
    char c;
    uint8_t col[5];
};

static const Glyph kFont[] = {
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}}, {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}}, {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}}, {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}}, {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}}, {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}}, {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}}, {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}}, {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}}, {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}}, {'m', {0x7C, 0x04, 0x78, 0x04, 0x78}},
    {'k', {0x7F, 0x10, 0x28, 0x44, 0x00}},
};

static const Glyph* FindGlyph(char c) {
    for (const auto& g : kFont) {
        if (g.c == c) {
            return &g;
        }
    }
    return nullptr;
}

static int TextWidth(const char* s, int scale) {
    int n = static_cast<int>(strlen(s));
    return n > 0 ? n * 6 * scale - scale : 0;
}

static void Text(int x, int y, const char* s, int scale, bool bold = false) {
    for (; *s; ++s, x += 6 * scale) {
        const Glyph* g = FindGlyph(*s);
        if (g == nullptr) {
            continue;
        }
        for (int c = 0; c < 5; ++c) {
            for (int r = 0; r < 7; ++r) {
                if (g->col[c] & (1u << r)) {
                    Box(x + c * scale, y + r * scale, scale + (bold ? 1 : 0), scale);
                }
            }
        }
    }
}

static void TextC(int cx, int y, const char* s, int scale, bool bold = false) {
    Text(cx - TextWidth(s, scale) / 2, y, s, scale, bold);
}

static void Flush() {
    if (g_eri_oled_panel == nullptr) {
        return;
    }
    if (memcmp(g_fb, g_sent, sizeof(g_fb)) == 0) {
        return;  // khong doi: khong tot bang thong I2C
    }
    memcpy(g_sent, g_fb, sizeof(g_fb));
    esp_lcd_panel_draw_bitmap(g_eri_oled_panel, 0, 0, kW, kH, g_fb);
}

// ================================================================ trang thai HUD

struct Hud {
    int nav = 0, spd = 0, lim = 0, over = 0, trn = 0, dst = -1, exitN = 0;
    int avg = 0, avgL = 0, alr = 0, alrD = -1, alrV = 0;
};

static Hud g_hud;
static uint32_t g_last_rx = 0;  // ms; 0 = chua co du lieu
static std::atomic<bool> g_sound_on{true};
static std::atomic<bool> g_demo{false};
static std::atomic<bool> g_connected{false};
static std::atomic<bool> g_dev_pending{false};
static std::atomic<bool> g_beep_req{false};

static uint32_t NowMs() { return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS); }

static void LogHeap(const char* stage) {
    ESP_LOGI(TAG, "Heap [%s]: free=%u min=%u largest_internal=%u", stage,
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(esp_get_minimum_free_heap_size()),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

// ================================================================ am thanh (chi bip khi vuot toc do)

static constexpr int kSampleRate = 16000;
static constexpr float kBeepVol = 0.1f;  // 0.0 - 1.0
static i2s_chan_handle_t g_tx = nullptr;
static int16_t g_sine[256];

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
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(kSampleRate));
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
    return i2s_channel_enable(g_tx) == ESP_OK;
}

// f = 0: im lang
static void PlayNote(uint16_t f, uint16_t ms) {
    static int16_t buf[2 * 160];
    static uint32_t phase = 0;
    const int total = (kSampleRate / 1000) * ms;
    const int ramp = kSampleRate / 200;  // 5 ms vao/ra de khong bi tach
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

static void AudioTask(void*) {
    while (true) {
        if (g_beep_req.exchange(false)) {
            PlayNote(2100, 100);
            PlayNote(0, 200);  // xa im lang de DMA khong lap lai am cuoi
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

// ================================================================ BLE (NimBLE)

// UUID 128-bit theo tai lieu HLP (viet nguoc byte)
static const ble_uuid128_t kSvcUuid = BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                                                       0x48, 0x4c, 0x6e, 0x4d, 0x01, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t kTxUuid = BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                                                      0x48, 0x4c, 0x6e, 0x4d, 0x02, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t kRxUuid = BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                                                      0x48, 0x4c, 0x6e, 0x4d, 0x03, 0x00, 0x7e, 0x8a);

static constexpr int kLineMax = 520;
static QueueHandle_t g_lineq = nullptr;
static uint16_t g_rx_handle = 0;
static uint16_t g_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t g_own_addr_type = 0;
static char g_acc[kLineMax];
static size_t g_acc_n = 0;

static const char kDevJson[] =
    R"({"v":1,"t":"dev","name":"WazeHUD-C3","fw":"1.0.0","proto":[1],)"
    R"("disp":{"w":128,"h":64,"color":0},)"
    R"("can":["speed","limit","turn","avgzone","alerts"],)"
    R"("want":{"rate":4,"fields":["nav","spd","lim","over","trn","dst",)"
    R"("exit","avg","avgL","alr","alrD","alrV"]}})";

// Gui 1 dong JSON ket thuc '\n' len Android, chia khuc theo MTU
static void SendLine(const char* s) {
    if (!g_connected || g_conn == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    char frame[600];
    int len = snprintf(frame, sizeof(frame), "%s\n", s);
    if (len <= 0) {
        return;
    }
    if (len >= static_cast<int>(sizeof(frame))) {
        len = sizeof(frame) - 1;
    }
    int mtu = ble_att_mtu(g_conn);
    int chunk = mtu > 23 ? mtu - 3 : 20;
    chunk = std::min(chunk, 240);
    for (int off = 0; off < len; off += chunk) {
        int n = std::min(chunk, len - off);
        for (int attempt = 0; attempt < 5; ++attempt) {
            os_mbuf* om = ble_hs_mbuf_from_flat(frame + off, n);
            if (om == nullptr) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            int rc = ble_gatts_notify_custom(g_conn, g_rx_handle, om);  // om duoc giai phong ben trong
            if (rc == 0) {
                break;
            }
            if (rc != BLE_HS_ENOMEM) {
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        vTaskDelay(pdMS_TO_TICKS(6));
    }
}

// Gom byte thanh dong (ket thuc '\n'), day sang HudTask de parse
static void FeedBytes(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char ch = static_cast<char>(d[i]);
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            g_acc[g_acc_n] = 0;
            if (g_acc_n > 0 && g_lineq != nullptr) {
                xQueueSend(g_lineq, g_acc, 0);
            }
            g_acc_n = 0;
        } else if (g_acc_n < kLineMax - 1) {
            g_acc[g_acc_n++] = ch;
        } else {
            g_acc_n = 0;  // tran: bo dong, dong bo lai o LF ke tiep
        }
    }
}

static int OnTxAccess(uint16_t, uint16_t, struct ble_gatt_access_ctxt* ctxt, void*) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t tmp[256];
        uint16_t out_len = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, tmp, sizeof(tmp), &out_len) == 0) {
            FeedBytes(tmp, out_len);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int OnRxAccess(uint16_t, uint16_t, struct ble_gatt_access_ctxt*, void*) {
    return BLE_ATT_ERR_READ_NOT_PERMITTED;  // chi dung notify
}

static struct ble_gatt_chr_def kChrs[] = {
    {
        .uuid = &kTxUuid.u,
        .access_cb = OnTxAccess,
        .flags = BLE_GATT_CHR_F_WRITE,  // write with response
    },
    {
        .uuid = &kRxUuid.u,
        .access_cb = OnRxAccess,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_rx_handle,
    },
    {},
};

static struct ble_gatt_svc_def kSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kSvcUuid.u,
        .characteristics = kChrs,
    },
    {},
};

static void StartAdv();

static int GapEvent(struct ble_gap_event* ev, void*) {
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE connect status=%d", ev->connect.status);
            if (ev->connect.status == 0) {
                g_conn = ev->connect.conn_handle;
                g_connected = true;
            } else {
                StartAdv();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnect reason=%d", ev->disconnect.reason);
            g_connected = false;
            g_dev_pending = false;
            g_conn = BLE_HS_CONN_HANDLE_NONE;
            StartAdv();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (ev->subscribe.attr_handle == g_rx_handle && ev->subscribe.cur_notify) {
                ESP_LOGI(TAG, "Android bat notify -> gui dev");
                g_dev_pending = true;
            }
            break;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU=%d", ev->mtu.value);
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            StartAdv();
            break;
        default:
            break;
    }
    return 0;
}

static void StartAdv() {
    struct ble_hs_adv_fields f = {};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char* name = ble_svc_gap_device_name();
    f.name = reinterpret_cast<const uint8_t*>(name);
    f.name_len = static_cast<uint8_t>(strlen(name));
    f.name_is_complete = 1;
    f.uuids128 = &kSvcUuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params p = {};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &p, GapEvent, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising as %s", name);
    }
}

static void OnSync() {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "addr setup rc=%d", rc);
        return;
    }
    StartAdv();
}

static void OnReset(int reason) { ESP_LOGW(TAG, "BLE reset reason=%d", reason); }

static void HostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool StartBle() {
    esp_err_t r = nimble_port_init();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(r));
        return false;
    }
    ble_hs_cfg.reset_cb = OnReset;
    ble_hs_cfg.sync_cb = OnSync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(kSvcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(kSvcs);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT setup rc=%d", rc);
        return false;
    }
    ble_svc_gap_device_name_set("WazeHUD");
    ble_att_set_preferred_mtu(247);
    nimble_port_freertos_init(HostTask);
    return true;
}

// ================================================================ parse HLP/1

static int JInt(cJSON* o, const char* key, int def) {
    cJSON* it = cJSON_GetObjectItem(o, key);
    return cJSON_IsNumber(it) ? it->valueint : def;
}

static void HandleLine(const char* s) {
    cJSON* doc = cJSON_Parse(s);
    if (doc == nullptr) {
        return;
    }
    if (JInt(doc, "v", 0) == 1) {
        cJSON* tj = cJSON_GetObjectItem(doc, "t");
        const char* t = cJSON_IsString(tj) ? tj->valuestring : "";
        if (strcmp(t, "ping") == 0) {
            SendLine(R"({"v":1,"t":"pong"})");
        } else if (strcmp(t, "hi") == 0) {
            g_hud = Hud();
            g_last_rx = NowMs();
        } else if (strcmp(t, "bye") == 0) {
            g_last_rx = 0;
        } else if (strcmp(t, "s") == 0) {
            g_hud.nav = JInt(doc, "nav", 0);
            g_hud.spd = JInt(doc, "spd", 0);
            g_hud.lim = JInt(doc, "lim", 0);
            g_hud.over = JInt(doc, "over", 0);
            g_hud.trn = JInt(doc, "trn", 0);
            g_hud.dst = JInt(doc, "dst", -1);
            g_hud.exitN = JInt(doc, "exit", 0);
            g_hud.avg = JInt(doc, "avg", 0);
            g_hud.avgL = JInt(doc, "avgL", 0);
            g_hud.alr = JInt(doc, "alr", 0);
            g_hud.alrD = JInt(doc, "alrD", -1);
            g_hud.alrV = JInt(doc, "alrV", 0);
            g_last_rx = NowMs();
            if (g_last_rx == 0) {
                g_last_rx = 1;
            }
        }
    }
    cJSON_Delete(doc);
}

// ================================================================ ve man hinh

struct AlertIcon {
    const uint8_t* bmp;  // nullptr neu khong co icon
    int maxM;            // khoang cach toi da (met) de bat dau hien
    bool speedNum;       // true: ve bien toc do co so (alrV)
};

static AlertIcon AlertIconFor(int a) {
    switch (a) {
        case 1: return {ICON_POLICE, 500, false};
        case 2: case 40: case 41: case 42: case 43: case 44: case 45: case 46:
            return {ICON_CAM, 500, false};
        case 3: return {ICON_LIGHT, 500, false};
        case 4: case 13: case 14: case 15: case 17: case 48:
            return {ICON_HAZARD, 500, false};
        case 5: return {ICON_CRASH, 500, false};
        case 6: return {ICON_JAM, 500, false};
        case 7: return {ICON_CLOSED, 500, false};
        case 9: return {ICON_NOPASS, 500, false};
        case 11: return {ICON_RAIL, 500, false};
        case 12: return {ICON_TOLL, 500, false};
        case 20: case 21: return {ICON_REST, 500, false};
        case 8: return {nullptr, 200, true};  // doi gioi han toc do (kem alrV)
        case 23: return {ICON_KDC_START, 200, false};
        case 24: return {ICON_KDC_END, 200, false};
        case 28: return {ICON_NOLEFT, 200, false};
        case 29: return {ICON_NORIGHT, 200, false};
        case 30: return {ICON_NOUTURN, 200, false};
        case 57: return {ICON_SCHOOL, 200, false};
        case 59: return {ICON_CURVE, 200, false};
        default: return {nullptr, 0, false};
    }
}

static void FmtDist(int m, char* out, size_t sz) {
    if (m < 1000) {
        snprintf(out, sz, "%dm", m);
    } else {
        snprintf(out, sz, "%d.%dkm", m / 1000, (m % 1000) / 100);
    }
}

// Hop chu nhat / tam giac, lat guong theo truc doc qua cx khi m = true
static void Bx(int cx, bool m, int x, int y, int w, int h) {
    if (m) {
        x = 2 * cx - x - w + 1;
    }
    Box(x, y, w, h);
}

static void Tr(int cx, bool m, int x1, int y1, int x2, int y2, int x3, int y3) {
    if (m) {
        x1 = 2 * cx - x1;
        x2 = 2 * cx - x2;
        x3 = 2 * cx - x3;
    }
    Tri(x1, y1, x2, y2, x3, y3);
}

static void DrawTurn(int cx, int cy, int trn, int ex) {
    bool left = (trn == 2 || trn == 4 || trn == 6 || trn == 13 || trn == 15);
    bool right = (trn == 3 || trn == 5 || trn == 7 || trn == 14 || trn == 16);
    if (left || right) {
        bool m = right;
        Bx(cx, m, cx + 2, cy - 2, 4, 14);
        Bx(cx, m, cx - 6, cy - 5, 12, 4);
        Tr(cx, m, cx - 12, cy - 3, cx - 4, cy - 11, cx - 4, cy + 5);
    } else if (trn == 1 || trn == 19) {  // di thang
        Box(cx - 2, cy - 4, 4, 16);
        Tri(cx, cy - 12, cx - 8, cy - 2, cx + 8, cy - 2);
    } else if (trn == 8 || trn == 9) {  // quay dau
        Box(cx + 4, cy - 4, 4, 16);
        Box(cx - 7, cy - 8, 15, 4);
        Box(cx - 7, cy - 8, 4, 12);
        Tri(cx - 5, cy + 12, cx - 13, cy + 3, cx + 3, cy + 3);
    } else if (trn == 10 || trn == 11 || trn == 12 || trn == 20) {  // vong xoay
        Ring(cx, cy, 11, 2);
        if (ex > 0) {
            char b[16];
            snprintf(b, sizeof(b), "%d", ex);
            TextC(cx, cy - 7, b, 2, true);
        }
    } else if (trn == 17) {  // den noi: la co ca-ro
        Box(cx - 7, cy - 11, 2, 23);
        Frame(cx - 5, cy - 11, 13, 9);
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 3; ++c) {
                if (((r + c) & 1) == 0) {
                    Box(cx - 4 + c * 4, cy - 10 + r * 4, 4, 4);
                }
            }
        }
    }
}

static void DrawSpeedSign(int cx, int cy, int v) {
    Ring(cx, cy, 17, 3);
    if (v <= 0) {
        return;
    }
    char b[16];
    snprintf(b, sizeof(b), "%d", v);
    if (v >= 100) {
        TextC(cx, cy - 3, b, 1, true);
    } else {
        TextC(cx, cy - 7, b, 2, true);
    }
}

static void DrawScreen(bool fresh) {
    memset(g_fb, 0, sizeof(g_fb));
    g_color = true;

    if (!fresh) {
        TextC(64, 14, "WAZE HUD", 2, true);
        TextC(64, 40, g_connected ? "WAIT DATA..." : "WAIT CONNECT BLE", 1);
        if (g_demo) {
            Text(0, 57, "DEMO", 1);
        }
        return;
    }

    char b[32];  // du cho moi gia tri int

    // Gioi han toc do: vong tron lon ben trai. Vuot toc do thi nhap nhay dac/dao mau.
    bool over = g_hud.over || (g_hud.lim > 0 && g_hud.spd > g_hud.lim);
    bool inv = over && ((NowMs() / 400) % 2 == 0);
    const int LX = 28, LY = 25, LR = 25;
    if (inv) {
        Disc(LX, LY, LR);
    } else {
        Ring(LX, LY, LR, 3);
    }
    g_color = !inv;
    if (g_hud.lim > 0) {
        snprintf(b, sizeof(b), "%d", g_hud.lim);
        int sc = strlen(b) >= 3 ? 2 : 3;
        TextC(LX, LY - (7 * sc) / 2, b, sc, true);
    } else {
        TextC(LX, LY - 10, "--", 3, true);
    }
    g_color = true;

    // Toc do hien tai o goc tren phai
    Text(66, 0, g_demo ? "DEMO" : "WAZE", 1);
    snprintf(b, sizeof(b), "%d", g_hud.spd);
    Text(126 - TextWidth(b, 2), 3, b, 2, true);

    // Ben phai: canh bao (uu tien) hoac mui ten re
    AlertIcon ai = {nullptr, 0, false};
    int adist = -1;
    if (g_hud.avg) {
        ai = {ICON_NOPASS, 100000, false};
        adist = g_hud.avgL;
    } else if (g_hud.alr > 0 && g_hud.alrD >= 0) {
        AlertIcon t = AlertIconFor(g_hud.alr);
        if ((t.bmp || t.speedNum) && g_hud.alrD <= t.maxM) {
            ai = t;
            adist = g_hud.alrD;
        }
    }

    char d[32];
    if (ai.bmp || ai.speedNum) {
        if (ai.speedNum) {
            DrawSpeedSign(95, 33, g_hud.alrV);
        } else {
            Icon(ai.bmp, 80, 17, 32);
        }
        FmtDist(adist, d, sizeof(d));
        TextC(96, 50, d, 2, true);
    } else if (g_hud.nav) {
        DrawTurn(78, 33, g_hud.trn, g_hud.exitN);
        if (g_hud.dst >= 0) {
            FmtDist(g_hud.dst, d, sizeof(d));
            TextC(96, 50, d, 2, true);
        }
    }

    if (!g_sound_on) {
        Text(0, 57, "MUTE", 1);
    }
}

// ================================================================ am thanh theo trang thai

static void UpdateSounds(bool fresh) {
    static bool was_over = false;
    static uint32_t last_beep = 0;

    bool over = fresh && (g_hud.over || (g_hud.lim > 0 && g_hud.spd > g_hud.lim));
    if (!over) {
        was_over = false;
        return;
    }
    // Bip ngay khi bat dau vuot; neu van vuot, lap lai moi 5 giay
    if (!was_over || NowMs() - last_beep >= 5000) {
        if (g_sound_on) {
            g_beep_req = true;
        }
        last_beep = NowMs();
    }
    was_over = true;
}

// ================================================================ demo

static void DemoStep() {
    static uint32_t t0 = 0;
    if (NowMs() - t0 < 700) {
        return;
    }
    t0 = NowMs();
    static int k = 0;
    ++k;
    static const int turns[] = {1, 2, 3, 8, 10, 17};
    static const int alerts[] = {2, 3, 1, 5, 6, 12, 4, 7, 8, 11, 20, 23, 24, 28, 29, 30, 57, 59};
    g_hud.nav = 1;
    g_hud.spd = 40 + (k * 3) % 40;
    g_hud.lim = 60;
    g_hud.trn = turns[(k / 6) % 6];
    g_hud.exitN = 2;
    g_hud.dst = 1500 - (k * 37) % 1400;
    g_hud.avg = (k / 10) % 2;
    g_hud.avgL = 300;
    g_hud.alr = ((k / 7) % 2) ? alerts[(k / 14) % 18] : 0;
    g_hud.alrD = 180;
    g_hud.alrV = 40;
    g_last_rx = NowMs();
    if (g_last_rx == 0) {
        g_last_rx = 1;
    }
}

// ================================================================ task chinh

static void HudTask(void*) {
    static char line[kLineMax];
    bool was_demo = false;
    while (true) {
        if (g_dev_pending.exchange(false)) {
            SendLine(kDevJson);
        }
        while (g_lineq != nullptr && xQueueReceive(g_lineq, line, 0) == pdTRUE) {
            HandleLine(line);
        }

        bool demo = g_demo;
        if (demo) {
            DemoStep();
        } else if (was_demo) {
            g_hud = Hud();
            g_last_rx = 0;
        }
        was_demo = demo;

        bool fresh = g_last_rx != 0 && (NowMs() - g_last_rx < 3000);
        UpdateSounds(fresh);
        DrawScreen(fresh);
        Flush();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ================================================================ giao dien

void Start() {
    LogHeap("hud start");

    // Dung LVGL: tu day ta ve thang len OLED bang bo dem rieng
    lvgl_port_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    if (g_eri_oled_panel == nullptr) {
        ESP_LOGE(TAG, "Khong co tay cam OLED");
        return;
    }
    memset(g_sent, 0xFF, sizeof(g_sent));  // ep ve khung dau tien

    for (int i = 0; i < 256; ++i) {
        g_sine[i] = static_cast<int16_t>(sinf(2.0f * 3.14159265f * i / 256.0f) * 32767.0f * kBeepVol);
    }
    if (InitI2s()) {
        xTaskCreate(AudioTask, "eri_beep", 3072, nullptr, 3, nullptr);
    } else {
        ESP_LOGW(TAG, "Khong co am thanh");
    }

    g_lineq = xQueueCreate(4, kLineMax);
    if (!StartBle()) {
        ESP_LOGE(TAG, "BLE khong khoi dong duoc");
    }
    LogHeap("ble started");
    xTaskCreate(HudTask, "eri_hud", 6144, nullptr, 5, nullptr);
}

void ToggleMute() {
    bool v = !g_sound_on.load();
    g_sound_on = v;
    ESP_LOGI(TAG, "Sound %s", v ? "on" : "off");
    if (v) {
        g_beep_req = true;  // bip xac nhan
    }
}

void ToggleDemo() {
    bool v = !g_demo.load();
    g_demo = v;
    ESP_LOGI(TAG, "Demo %s", v ? "on" : "off");
}

}  // namespace EriHud
