#pragma once
// Bo ve do hoa 1-bit cho OLED 128x64 (SSD1306, bo dem theo trang).
// File nay KHONG phu thuoc ESP-IDF nen co the thu tren may tinh.

#include <stdint.h>

#include "eri_gfx_fonts.h"
#include "eri_gfx_icons.h"

namespace EriGfx {

constexpr int kW = 128;
constexpr int kH = 64;
constexpr int kBufBytes = kW * kH / 8;

const uint8_t* Buffer();      // byte = (y/8)*128 + x, bit = y%8
void Clear();
void SetColor(bool on);       // true = sang, false = xoa (dung de ve chu dao mau)

void Pix(int x, int y);
void Box(int x, int y, int w, int h);
void Frame(int x, int y, int w, int h);
void RoundBox(int x, int y, int w, int h, int r);    // dac, bo goc
void RoundFrame(int x, int y, int w, int h, int r);  // vien 1px, bo goc
void Line(int x0, int y0, int x1, int y1);
void Disc(int cx, int cy, int r);
void Ring(int cx, int cy, int r, int thickness);
void Tri(int x1, int y1, int x2, int y2, int x3, int y3);

void Draw(const Bmp& b, int x, int y);                   // anh MSB-first
void DrawXbm(const uint8_t* xbm24, int x, int y, int size);  // icon XBM 24x24 (LSB truoc), co the thu/phong

int TextWidth(const Font& f, const char* s);
// y = duong co so (baseline) cua chu
void Text(const Font& f, int x, int baseline, const char* s);
void TextC(const Font& f, int cx, int baseline, const char* s);  // can giua theo cx
void TextR(const Font& f, int right, int baseline, const char* s);  // can phai
int CapHeight(const Font& f);  // chieu cao chu so/chu hoa (pixel tren duong co so)

}  // namespace EriGfx
