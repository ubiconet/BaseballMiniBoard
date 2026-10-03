# Baseball MiniBoard

Manual-only ESP32-S3 baseball scoreboard with three MAX7219 8×8 matrices
(Away score, Inning, Home score) and seven discrete count LEDs (3 balls,
2 strikes, 2 outs). The board does not retrieve game or team data from an
online service: update the scoreboard from the Manual Controls page.

## Manual controls

The setup page links to **Manual Controls** and **Display Test**. The manual
page adjusts the home and away scores, inning, top/bottom half, and ball,
strike, and out counts. **Reset Count** clears only the count LEDs;
**Reset All** restores a 0–0 score, top of the first, and an empty count.
Scoreboard state is saved in nonvolatile storage and restored after reboot.
Manual count LEDs always show the saved game count, independent of Wi-Fi
connectivity.

## Setup and connectivity

At boot, the board starts its setup access point and captive portal. Configure
Wi-Fi on the setup page; the access point and portal remain available after
the board connects to the configured network. The portal also provides
firmware update controls and a per-pixel/per-LED display test.

During a firmware update, all seven count LEDs flash at 250 ms intervals and
the center matrix displays `UD`.

## Hardware

| Output | Connection |
|---|---|
| MAX7219 matrices (Away, center, Home) | DIN=GPIO14, CLK=GPIO8, CS=GPIO16 |
| Ball LEDs 1–3 | GPIO7, GPIO6, GPIO5 |
| Strike LEDs 1–2 | GPIO4, GPIO3 |
| Out LEDs 1–2 | GPIO2, GPIO1 |

## Build

```powershell
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run `
    --environment esp32-s3-devkitc-1
```

The OTA upload environment is `esp32-s3-devkitc-1-ota`. A release build is
created with `pio run --environment esp32-s3-devkitc-1 --target deploy`.
The deploy target reads `FIRMWARE_VERSION` from `src/config.h`, creates
archived/latest binaries and `releases/manifest.json`, then pushes the release
artifacts to this repository.
