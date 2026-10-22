# eri-c3-voice

ESP32-C3 + INMP441 (mic) + MAX98357A (ampli) + OLED SSD1306 128x64 + 2 nut bam.

## Cai dat

1. Copy thu muc nay vao `main/boards/eri-c3-voice/` cua repo xiaozhi-esp32.

2. `main/Kconfig.projbuild`, them vao khoi `choice BOARD_TYPE`:

```
    config BOARD_TYPE_ERI_C3_VOICE
        bool "Eri C3 Voice"
        depends on IDF_TARGET_ESP32C3
```

3. `main/CMakeLists.txt`, them nhanh moi vao chuoi elseif:

```
elseif(CONFIG_BOARD_TYPE_ERI_C3_VOICE)
    set(BOARD_DIR "eri-c3-voice")
    set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)
    set(BUILTIN_ICON_FONT font_awesome_14_1)
```

4. Build:

```
python scripts/build.py eri-c3-voice
```

## Chan

| Chuc nang | GPIO |
|---|---|
| I2S BCLK (loa + mic) | 2 |
| I2S WS (loa + mic) | 1 |
| Loa DIN | 3 |
| Mic SD | 8 |
| OLED SDA / SCL | 21 / 20 |
| Nut hoi thoai | 4 |
| Nut am luong | 10 |

Nho noi L/R cua INMP441 xuong GND.
