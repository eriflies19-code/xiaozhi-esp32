# Them menu Chat AI / Radio cho eri-c3-voice

## 1. Chep file vao C:\xz\main\boards\eri-c3-voice\

Chep tat ca file nhan duoc vao thu muc nay (ghi de `eri_c3_voice_board.cc`).
KHONG ghi de `config.h` va `config.json` (giu cau hinh cua ban).

Cac file moi: eri_mode.h/.cc, eri_radio.h/.cc, eri_mp3.h/.cc, eri_stations.h

## 2. Dien danh sach dai trong eri_stations.h

Thay 2 dai vi du bang dai that. Yeu cau: http:// (chua ho tro https), dinh dang MP3.

## 3. Sua C:\xz\main\main.cc (2 cho)

Ngay duoi dong `#include "application.h"` them:

    #ifdef CONFIG_BOARD_TYPE_ERI_C3_VOICE
    #include "boards/eri-c3-voice/eri_mode.h"
    #endif

Trong app_main, ngay duoi dong `ESP_ERROR_CHECK(ret);` va truoc dong
`// Initialize and run the application` them:

    #ifdef CONFIG_BOARD_TYPE_ERI_C3_VOICE
        if (EriMode::Run()) {
            return;  // Radio chay trong task rieng, khong khoi dong Application
        }
    #endif

## 4. Build (file moi can cau hinh lai de duoc nhan)

Trong terminal ESP-IDF:

    cd C:\xz
    (Get-Item C:\xz\main\CMakeLists.txt).LastWriteTime = Get-Date
    cd C:\xz\build
    ninja -j1 *> C:\xz\out2.txt
    Select-String -Path C:\xz\out2.txt -Pattern "error:" | Select-Object -First 10
    Get-Content C:\xz\out2.txt | Select-Object -Last 5

## 5. Nap

    cd C:\xz
    $env:IDF_TARGET = "esp32c3"
    idf.py -p COM3 flash monitor

## Cach dung

- Bat may: OLED hien "[CHAT AI]  radio", dem nguoc 5 giay, tu vao che do dung lan truoc.
- GPIO10: doi muc. GPIO4: xac nhan. Bam bat ky nut nao thi dung dem nguoc.
- Che do Radio: GPIO4 bam = dai ke tiep, GPIO10 bam = tang am luong (vong lai), giu = giam.
- Moi che do: giu GPIO4 2 giay = khoi dong lai ve menu.
- Radio dung chung WiFi da cai o che do Chat AI.
