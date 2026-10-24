#pragma once

#include <cstdint>

// Boc goi bo giai ma MP3 cua esp_audio_codec (simple decoder).
// Moi phu thuoc vao API cua thu vien nam gon trong eri_mp3.cc de de sua neu
// ten ham/kieu khac phien ban.
class Mp3Decoder {
public:
    ~Mp3Decoder();

    bool Open();
    void Close();
    bool Reset();

    // Giai ma tu `in`. Tra ve true neu giai ma OK.
    // *consumed: so byte da dung, *out_bytes: so byte PCM 16-bit da tao.
    bool Decode(const uint8_t* in, int in_len, int* consumed, uint8_t* out, int out_cap,
                int* out_bytes);

    // Thong tin luong (co sau khung hinh dau tien).
    bool GetInfo(int* sample_rate, int* channels, int* bits);

private:
    void* handle_ = nullptr;
};
