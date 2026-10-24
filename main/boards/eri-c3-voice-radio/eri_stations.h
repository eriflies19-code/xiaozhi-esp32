#pragma once

// DANH SACH DAI RADIO - bam GPIO4 de doi dai theo thu tu nay.
//
// Yeu cau cua tung URL:
//  - Bat dau bang http:// (CHUA ho tro https://)
//  - Dinh dang MP3 (khong phai AAC/OGG)
//  - Ghi ro cong neu co, vi du http://host:8000/duong-dan
//  - name: viet khong dau, ngan (OLED nho)
//
// Hai dong duoi day CHI LA VI DU, khong phai dai that. Hay thay bang
// danh sach dai trong ban radio cu cua ban.

struct EriStation {
    const char* name;
    const char* url;
};

static const EriStation kStations[] = {
    {"Dai 1 (vi du)", "http://c13.radioboss.fm:8127/stream"},
    {"Dai 2 (vi du)", "https://ice2.somafm.com/groovesalad-128-mp3"},
};
