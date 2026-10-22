# Eri C3 Voice + Internet Radio (experimental)

Separate ESP-IDF board variant for the Eri C3 Voice hardware. It preserves the
existing `eri-c3-voice` XiaoZhi build and adds a startup selector plus an
experimental Internet-radio mode.

## Build

Select `eri-c3-voice-radio` in the board build script:

```powershell
python scripts/build.py eri-c3-voice-radio
```

At startup, click GPIO4 for XiaoZhi or GPIO10 for Internet Radio. If no button
is clicked within 10 seconds, the firmware starts XiaoZhi.

Radio mode currently supports Wi-Fi configured through XiaoZhi's existing Wi-Fi
manager, five built-in MP3 stations, GPIO4 for the next station, and GPIO10 for
volume. It does not yet include the original sketch's web controls, lyrics,
custom stations, or Waze HUD/BLE.

The 22.5 KB stream buffer is allocated only in radio mode. Test heap headroom
and playback on the actual ESP32-C3 hardware before relying on this experimental
variant.
