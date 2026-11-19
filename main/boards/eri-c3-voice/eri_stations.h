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
    {"London Soul",  "http://londonsoulradio.out.airtime.pro:8000/londonsoulradio_a"},
    {"RFI TV", "https://rfienvietnamien64k.ice.infomaniak.ch/rfienvietnamien-64.mp3"},
    {"FM 90 Ha Noi", "http://14.162.146.90:8000/HANOI90"},
    {"Cherry Music", "https://stream-176.zeno.fm/umt5gqmg3reuv"},
    {"RVA", "https://livestream.rvasia.org:8443/Vietnamese.mp3"},
    {"China Music", "https://lhttp.qtfm.cn/live/20500187/64k.mp3"},
    {"2000FM Country", "http://bigrradio.cdnstream1.com/5103_128"},
    {"181.FM", "http://listen.181fm.com/181-power_128k.mp3"},
    {"Capital FM London", "http://media-ice.musicradio.com/CapitalMP3"},
    {"Radio Caroline", "http://78.129.202.200:8040/;"},
    };
