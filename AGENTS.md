# AGENTS.md — BaseballMiniBoard

Operating notes for software changes to the manual-only Baseball MiniBoard.
Keep changes small, verifiable, and consistent with the ESP32-S3 architecture.

## Project summary

- Hardware: ESP32-S3 DevKitC-1, 4 MB flash.
- Three cascaded MAX7219 8×8 matrices: Away score, center inning, Home score.
  DIN=GPIO14, CLK=GPIO8, CS=GPIO16.
- Seven discrete count LEDs: Balls 1–3 on GPIO7,6,5; Strikes 1–2 on
  GPIO4,3; Outs 1–2 on GPIO2,1.
- No TFT and no automatic game-data service. Scores, inning/half, and counts
  are entered from the Manual Controls page and persisted across reboots.
- Wi-Fi setup AP and captive portal remain available while the station is
  connected. OTA status, manual controls, and display test are accessible
  from the portal.
- PlatformIO environments: `esp32-s3-devkitc-1` (USB build) and
  `esp32-s3-devkitc-1-ota` (ArduinoOTA upload).

## Source architecture

```text
src/
├── main.cpp                 # Network bring-up, OTA indicator, app loop
├── config.h                 # Version and repository-specific OTA URLs
├── common/
│   ├── app/sport_api.h      # App/profile contract
│   ├── comms/               # Wi-Fi portal and remote firmware updates
│   ├── data/snapshot_channel.h # Core-0 to core-1 POD mailbox
│   ├── hal/                 # MAX7219 matrices and count LEDs
│   └── ui/                  # Display-test and OTA indicators
└── sports/baseball/
    ├── sport_config.h       # Hardware pins and portal branding
    ├── baseball_app.cpp     # Manual-state initialization and render loop
    ├── baseball_renderer.*  # Score, inning/half, and count LED output
    └── manual_control.*    # Portal page, HTTP handlers, and NVS state
```

The common framework stays independent of baseball rules. The selected app
profile provides its labels and registers its manual-control routes through
the portal callback.

## Runtime and persistence

- Core 1 (`loop()`): renders from `SnapshotChannel<ManualGameState>` and owns
  all display writes. Do not write LEDs or matrices from a web request handler.
- Core 0: services Wi-Fi, the captive portal, development OTA, and remote
  firmware self-updates. Manual HTTP handlers persist state to NVS and publish
  a snapshot for the render loop.
- Initial state is home 0, away 0, inning 1 Top, with an empty count.
- Score and inning values are bounded to 0–99 and 1–99; ball, strike, and out
  counts are bounded to 3, 2, and 2.
- **Reset Count** preserves score and inning. **Reset All** restores the
  initial state. Count LEDs always reflect the saved manual count, regardless
  of network connectivity.
- No network requests or dynamic allocations belong in `sport::tick()`.

## Build, version, and release

Build firmware without uploading:

```powershell
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run `
    --environment esp32-s3-devkitc-1
```

Build the OTA environment:

```powershell
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run `
    --environment esp32-s3-devkitc-1-ota
```

Create and publish a release with:

```powershell
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run `
    --environment esp32-s3-devkitc-1 --target deploy
```

The deploy target reads `FIRMWARE_VERSION` from `src/config.h`, writes the
archived/latest binary and manifest in `releases/`, and pushes the release
artifacts. The device installs only a strictly newer manifest version.

Every firmware change requires a version bump. Increment the minor number for
compatible changes. Bump the major for a breaking user-visible change, after
confirming the change with the user. Keep the version string in `src/config.h`.

Do not flash a device unless the user explicitly requests it. For release
verification, build both environments, verify flash use stays below the
1.5 MiB OTA partition, and confirm the public manifest and binary are
reachable.

## Coding conventions

- Keep the render path allocation-free and nonblocking.
- Keep hardware pin assignments in `sports/baseball/sport_config.h`.
- Use `DBG_PRINTF` for optional diagnostics; do not add frequent Serial output
  to `loop()` or rendering code.
- Persist user-controlled game state before publishing it to the renderer.
- Validate all portal inputs and return explicit HTTP errors for invalid
  requests or failed NVS writes.
- Preserve the setup AP+STA behavior and OTA update indicator.

## Validation checklist

1. Run `git diff --check`.
2. Build both PlatformIO environments and confirm the flash partition limit.
3. Verify the portal has no team-selection fields and both feature links are
   buttons.
4. Verify manual score, inning, half, and count controls update the correct
   matrices/LEDs; test both reset operations.
5. Reboot verification requires hardware: confirm saved state is restored.
6. Do not claim physical behavior was verified unless the device was tested.
