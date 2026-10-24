#pragma once

// Menu chon che do khi bat may: Chat AI (Xiaozhi) hoac Radio.
// Header nay cố ý nhe (khong include board.h) de main.cc co the include an toan.

namespace EriMode {

enum class Mode { kMenu, kChat, kRadio };

// Hien menu (5 giay). Tra ve true neu da chon Radio va radio dang chay
// trong cac task rieng -> app_main nen return, KHONG khoi dong Application.
// Tra ve false neu chon Chat AI -> tiep tuc khoi dong Xiaozhi nhu binh thuong.
bool Run();

Mode Current();

// Cac ham xu ly nut. Tra ve true neu da xu ly xong (board khong can lam gi them).
bool HandleChatClick();         // GPIO4 bam
bool HandleChatLongPress();     // GPIO4 giu 2 giay -> quay lai menu
bool HandleVolumeClick();       // GPIO10 bam
bool HandleVolumeLongPress();   // GPIO10 giu

void RestartToMenu();

}  // namespace EriMode
