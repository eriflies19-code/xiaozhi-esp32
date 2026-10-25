#include "eri_gfx.h"

#include <string.h>

#include <algorithm>
#include <cstdlib>

namespace EriGfx {

static uint8_t g_fb[kBufBytes];
static bool g_on = true;

const uint8_t* Buffer() { return g_fb; }
void Clear() { memset(g_fb, 0, sizeof(g_fb)); }
void SetColor(bool on) { g_on = on; }

void Pix(int x, int y) {
    if (static_cast<unsigned>(x) >= kW || static_cast<unsigned>(y) >= kH) {
        return;
    }
    uint8_t& b = g_fb[(y >> 3) * kW + x];
    if (g_on) {
        b |= static_cast<uint8_t>(1u << (y & 7));
    } else {
        b &= static_cast<uint8_t>(~(1u << (y & 7)));
    }
}

void Box(int x, int y, int w, int h) {
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            Pix(x + i, y + j);
        }
    }
}

void Frame(int x, int y, int w, int h) {
    Box(x, y, w, 1);
    Box(x, y + h - 1, w, 1);
    Box(x, y, 1, h);
    Box(x + w - 1, y, 1, h);
}

// Diem (px,py) co nam trong hinh chu nhat bo goc ban kinh r khong
static bool InRound(int px, int py, int w, int h, int r) {
    int cx = px < r ? r : (px >= w - r ? w - 1 - r : px);
    int cy = py < r ? r : (py >= h - r ? h - 1 - r : py);
    int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r + r;
}

void RoundBox(int x, int y, int w, int h, int r) {
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            if (InRound(i, j, w, h, r)) {
                Pix(x + i, y + j);
            }
        }
    }
}

void RoundFrame(int x, int y, int w, int h, int r) {
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            if (InRound(i, j, w, h, r) && !InRound(i - 1, j - 1, w - 2, h - 2, std::max(r - 1, 0))) {
                Pix(x + i, y + j);
            }
        }
    }
}

void Line(int x0, int y0, int x1, int y1) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        Pix(x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Disc(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r + r) {
                Pix(cx + dx, cy + dy);
            }
        }
    }
}

void Ring(int cx, int cy, int r, int t) {
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

static int Edge(int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

void Tri(int x1, int y1, int x2, int y2, int x3, int y3) {
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

void Draw(const Bmp& b, int x, int y) {
    int rowb = (b.w + 7) / 8;
    for (int j = 0; j < b.h; ++j) {
        for (int i = 0; i < b.w; ++i) {
            if (b.data[j * rowb + i / 8] & (0x80 >> (i & 7))) {
                Pix(x + i, y + j);
            }
        }
    }
}

void DrawXbm(const uint8_t* src, int x, int y, int size) {
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

static const GlyphInfo* Find(const Font& f, unsigned char c) {
    if (c < f.first || c >= f.first + f.count) {
        return nullptr;
    }
    return &f.glyphs[c - f.first];
}

int TextWidth(const Font& f, const char* s) {
    int w = 0;
    for (; *s; ++s) {
        const GlyphInfo* g = Find(f, static_cast<unsigned char>(*s));
        if (g != nullptr) {
            w += g->adv;
        }
    }
    return w;
}

void Text(const Font& f, int x, int baseline, const char* s) {
    for (; *s; ++s) {
        const GlyphInfo* g = Find(f, static_cast<unsigned char>(*s));
        if (g == nullptr) {
            continue;
        }
        int rowb = (g->w + 7) / 8;
        for (int j = 0; j < g->h; ++j) {
            for (int i = 0; i < g->w; ++i) {
                if (f.bitmap[g->off + j * rowb + i / 8] & (0x80 >> (i & 7))) {
                    Pix(x + g->xo + i, baseline + g->yo + j);
                }
            }
        }
        x += g->adv;
    }
}

void TextC(const Font& f, int cx, int baseline, const char* s) {
    Text(f, cx - TextWidth(f, s) / 2, baseline, s);
}

void TextR(const Font& f, int right, int baseline, const char* s) {
    Text(f, right - TextWidth(f, s), baseline, s);
}

int CapHeight(const Font& f) {
    const GlyphInfo* g = Find(f, '0');
    if (g == nullptr) {
        g = Find(f, 'H');
    }
    return g != nullptr ? -g->yo : f.ascent;
}

}  // namespace EriGfx
