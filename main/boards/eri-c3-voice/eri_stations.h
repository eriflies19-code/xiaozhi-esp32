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
    {"Vietnam Nation", "http://c13.radioboss.fm:8127/stream"},
    {"Groove Salad", "https://ice2.somafm.com/groovesalad-128-mp3"},
    {"VIET WASHINGTON", "http://varadio.serverroom.net:7968/"},
    {"Music Lake",     "http://nap.casthost.net:8626/stream" },
    {"FOLK MUSIC", "https://ice5.somafm.com/folkfwd-128-mp3" },
    {"RADIO PARADISE", "http://stream-dc1.radioparadise.com/mp3-32"},
    {"Black ROCK",  "https://ice5.somafm.com/brfm-128-mp3"},
};
