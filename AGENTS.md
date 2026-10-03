# AGENTS.md — Operating instructions for AI coding assistants

This file describes how AI assistants should work on the
**BaseballMiniBoard** project. The goal: keep changes small, verifiable,
and consistent with the existing architecture; produce a build that an
OTA-updated device can run without surprises.

---

## 1. Project summary

- **Target hardware:** ESP32-S3 DevKitC-1 (4 MB flash).
- **Peripherals (this build is headless — no TFT):**
  - 3× MAX7219 8×8 LED matrices (DIN=14, CLK=8, CS=16). Device 0 = Away,
    Device 1 = Inning, Device 2 = Home. Live-game scores and inning/half;
    outer matrices show hour/minute as an idle clock (portal setting can
    disable the clock).
  - 7 discrete count LEDs on GPIO 1–7 (balls/strikes/outs).
- **Origin:** forked from the MLBScoreboard repo (v2.60) with the ST7789
  TFT and everything that only fed it removed. See README.md for the list
  of what went away.
- **Firmware:** Arduino-ESP32 framework, PlatformIO build system.
- **Build envs:**
  - `esp32-s3-devkitc-1` → USB CDC upload.
  - `esp32-s3-devkitc-1-ota` → ArduinoOTA Wi-Fi upload.

### Template architecture

This repo is a **sport scoreboard template**: a generic framework plus one
sport implementation, so new sports (NHL, etc.) are started by copying the
repo and replacing the sport folder.

```
src/
├── main.cpp              # GENERIC app shell: network bring-up, OTA
│                         #   indicator, NTP sync → sport::tick()
├── config.h              # composition root: FIRMWARE_VERSION (deploy script
│                         #   reads it HERE), timezone, OTA manifest URLs
├── common/               # GENERIC framework — no sport knowledge
│   ├── config.h          # framework defaults (SB_DEBUG gate, network/
│   │                     #   OTA pacing)
│   ├── app/sport_api.h   # THE contract a sport implements (namespace sport)
│   ├── hal/              # led_matrix (3× MAX7219: scores and clock),
│   │                     #   count_leds (7 counter LEDs)
│   ├── comms/            # http_fetcher (keep-alive HTTP + buffered parse),
│   │                     #   network_service (Wi-Fi/portal/NVS, branding +
│   │                     #   team options injected), ota_update
│   ├── data/             # snapshot_channel.h (cross-core mailbox template),
│   │                     #   time_util (NTP/ISO-8601 helpers)
│   └── ui/               # ota_indicator (UD + flashing count LEDs)
└── sports/
    └── mlb/              # THE SPORT — replaced wholesale per new sport
        ├── sport_config.h  # pins, branding, poll cadences
        ├── mlb_app.cpp     # WAITING/LIVE state machine + sport:: contract
        ├── mlb_renderer.cpp # score matrices + count LEDs from snapshots
        ├── mlb_state.*     # snapshot channels, activeGamePk,
        │                   #   fetch diagnostics
        ├── mlb_teams.*     # ONE team table {id, abbrev, label}
        ├── mlb_client.*    # feed endpoints + JSON filters (uses http_fetch)
        ├── mlb_data_task.cpp # core-0 fetch/publish loop
        └── mlb_snapshot.h    # POD snapshot structs (the core0→core1 contract)
```

**Sport selection:** each PlatformIO env sets
`build_src_filter = +<main.cpp>, +<common/>, +<sports/mlb/>` and
`-Isrc/sports/mlb`, so `#include "sport_config.h"` resolves to whichever
sport the env selects. Common code NEVER names a sport (the only
common→sport reach is the include-path-resolved `sport_config.h` seam plus
the `sport::` function contract in `common/app/sport_api.h`).

**Starting a new sport:** see the checklist in README.md. Short version —
copy `src/sports/mlb` → `src/sports/<sport>`, rewrite the sport folder,
point the env's src filter + `-I` at it, update `OTA_*` URLs in
`src/config.h` and `RAW_BASE`/`LATEST_FILE` in `tools/release_deploy.py`.

### Runtime architecture

- **Core 1 (Arduino `loop()`):** pure LED output. The generic shell runs
  the OTA indicator and one-shot NTP sync, then calls `sport::tick()`;
  the sport renders from POD snapshots via `take*Snapshot()`. No HTTP on
  this core.
- **Core 0 (FreeRTOS tasks):** the network task (portal/DNS/ArduinoOTA,
  priority 2) and the MLB data task (`sports/mlb/mlb_data_task.cpp`,
  priority 1) do all API calls, JSON parsing, and snapshot publishing.
- **User feedback without a screen:** setup instructions and status go to
  Serial (`[NET]`, `[BOOT]`, `[DISPLAY]` lines); the captive portal is
  discoverable from any phone; firmware updates show "UD" on the Inning
  matrix while all seven count LEDs flash.

---

## 2. Build & deploy workflow

```powershell
# Compile (no upload) — fast iteration on code
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run `
    --environment esp32-s3-devkitc-1

# USB upload (reliable; preferred when developing)
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run `
    --target upload --environment esp32-s3-devkitc-1

# OTA upload (Wi-Fi; flaky on weak signal — see notes below)
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run `
    --target upload --environment esp32-s3-devkitc-1-ota `
    --upload-port <device-ip>
```

**OTA caveats** (inherited from the parent project's network):
- Wi-Fi can be variable; OTA uploads may stall and take minutes.
- `--timeout=120` is set in `platformio.ini` for the OTA env.
- If OTA stalls repeatedly, **power-cycle the device** (or flash over
  USB) before retrying — leftover OTA state can wedge the bootloader.

### Releases + firmware self-update

- **One-command release:** `pio run -e esp32-s3-devkitc-1 -t deploy`.
  This builds the firmware, writes `releases/`
  (`baseball_miniboard_latest.bin`, permanent
  `baseball_miniboard_<version>.bin`, `manifest.json` with
  `{"version","file","url"}`), then **commits `releases/` and pushes to
  GitHub** — the push is what publishes the update. The target refuses to
  deploy when `FIRMWARE_VERSION` still matches the published manifest
  version (devices only flash strictly newer versions), so **bump
  `FIRMWARE_VERSION` in `src/config.h` before every deploy**. GitHub's
  raw CDN caches the manifest ~5 min after a push.
- **GitHub remote:** this fork starts with **no `origin` remote** so a
  deploy can never push into the MLBScoreboard repo. Create the
  `ubiconet/BaseballMiniBoard` GitHub repo (matching the URLs already in
  `src/config.h` and `tools/release_deploy.py`) and `git remote add
  origin …` before the first deploy.
- **Self-update flow** (`src/common/comms/ota_update.cpp`): shortly after
  the network comes online (before any feed fetch — the TLS handshake
  needs the pristine boot heap), the core-0 data task fetches the manifest
  over TLS (the only TLS connection; the feeds run plain HTTP — see the
  note at the top of `sports/mlb/mlb_client.cpp` and
  `common/comms/http_fetcher.h`). If the manifest version is strictly
  newer than `FIRMWARE_VERSION`, manifest and binary download over ONE
  reused TLS session while the Inning matrix shows "UD" and all count LEDs
  flash
  (`common/ui/ota_indicator.cpp`), then the device reboots into the new
  image. Failures leave the current firmware running and retry (2 tries
  in the boot window, then every 10 min).

---

## 3. Build-versioning rules — **MANDATORY on every code change**

`src/config.h` (the composition root) carries a single human-readable
version string:

```cpp
static const char* FIRMWARE_VERSION = "v1.0";
```

There is no boot splash to draw it on; the version appears in the Serial
boot banner (`[BOOT] FW=…`) and on the portal's Firmware Update panel.
`tools/release_deploy.py` reads it from `src/config.h` — keep the
definition in that file.

**Every change that ships to the device must bump `FIRMWARE_VERSION`.**

### Bump policy

- Increment the **minor** number with each build (i.e. the digit after
  the dot). Example: `v1.0` → `v1.1` → `v1.2` → …
- Don't bump the major unless the change is a breaking rewrite of the
  user-facing behavior. Ask the user before doing a major bump.
- Reset the minor back to `0` when the major goes up.
- The version is purely a string — keep it short, ASCII. Do not add build
  dates or commit SHAs to the string; the firmware compile date is already
  available via `__DATE__`.

### When to bump

| Change | Bump? |
|---|---|
| Bug fix that doesn't change user-visible behavior | **yes**, minor++ |
| New diagnostic info | yes, minor++ |
| Tweaks to internal data flow / caching | yes, minor++ |
| Comment-only / formatting / refactor with no behavior change | **yes**, minor++ (still ships, still counts as a build) |
| User-facing feature change | yes, minor++ |
| Breaking change to display behavior or APIs | ask first; major++ |

---

## 4. Coding conventions

- **No new heap allocations in `loop()`/`sport::tick()`.** The LED outputs
  are written through small static drivers; don't add `String`/JSON
  docs/etc. on the render path.
- **Feed/API calls live in core 0** (`sports/mlb/mlb_data_task.cpp` +
  `mlb_client.cpp`). Do not fetch from core 1. If you need new MLB data,
  add a field to a struct in `sports/mlb/mlb_snapshot.h`, publish through
  the channels in `sports/mlb/mlb_state.cpp` (or
  `common/data/snapshot_channel.h` for a new channel), and read it via a
  `take*Snapshot()` accessor.
- **Common code must stay sport-agnostic.** `src/common/` and
  `src/main.cpp` never include a sport header other than the
  include-path-resolved `sport_config.h` (constants only) and never call
  anything outside the `sport::` contract in `common/app/sport_api.h`.
  Sport logic belongs in `src/sports/<sport>/`.
- **Serial.printf is expensive** at 115200 baud (~25 ms per call). Use
  `DBG_PRINTF(fmt, ...)` (defined in `src/common/config.h`) instead — it
  compiles to a no-op when `SB_DEBUG == 0`. Production builds default
  `SB_DEBUG` to 0; leave it at 1 temporarily only while diagnosing an
  issue.
- **No `Serial.println` in hot paths** (`loop()`/`sport::tick()`). All
  status prints go through `DBG_PRINTF` and only fire on state
  transitions.
- **Pin assignments live in the sport's `sport_config.h`** and are passed
  to the HAL at init (`initLedMatrix(din, clk, cs)`,
  `initCountLeds(pins)`). `src/common/` contains no pin constants. Don't
  change pins without verifying against the actual hardware.

---

## 5. Testing before claiming a fix works

After any non-trivial change:

1. `pio run --environment esp32-s3-devkitc-1` — must compile with no
   errors or warnings. Also sanity-check flash size against the 1.5 MB
   OTA partition (the build prints the percentage; v1.0 sits ~66%).
2. Upload to the device (OTA or USB).
3. Confirm via Serial Monitor (with `-DSB_DEBUG=1`):
   - The boot banner shows `[BOOT] FW=<new version>`.
   - The count-LED sweep runs at boot, then clears.
   - `[NET] Online: ip=…` appears, and the waiting-mode status line
     (`[DISPLAY] state=WAITING … sched: att=N ok=M err=ok`) confirms the
     data task is publishing schedule fetches.
   - The outer MAX7219 matrices show the idle time (hour on home, minute on
     away) once NTP syncs; the Inning matrix stays blank.
   - During a live preferred-team game: matrices show away/home scores and
     inning/half,
     count LEDs track balls/strikes/outs (clearing between half innings).

---

## 6. Things to avoid (lessons learned)

- **Don't block in `loop()`/`sport::tick()`.** Every API call belongs on
  core 0.
- **Don't keep large `JsonDocument`s on the render core.** The render
  path only touches POD snapshots.
- **Don't open a second TLS connection while one is alive.** The OTA
  updater reuses one session for manifest + binary on purpose; the feed
  client closes its keep-alive session (`http_fetch::closeSession()`)
  before anything else connects.
- **Don't point the OTA URLs or `RAW_BASE` at the MLBScoreboard repo.**
  A device that self-updates from the wrong stream flashes the TFT
  firmware onto a board with no screen. The URLs in `src/config.h` and
  `tools/release_deploy.py` must always name THIS repo.
- **Don't bump `FIRMWARE_VERSION` past `v9.x` without a discussion.**
  Two-digit minors look weird in the banner. Plan a major bump before
  then.
- **Don't add pin constants or sport names to `src/common/`.** That
  breaks the template separation.

---

## 7. Repo memory

Long-lived project notes live under `/memories/repo/` in the assistant's
memory for this workspace. Seed content for this fork lives in the parent
project's notes (MLBScoreboard: pin assignments, MLB API integration
details, TLS heap churn, performance notes, the v2.59 template
reorganization).

When you discover something durable about this project (a build quirk, a
library version pin, a hardware pitfall), **write it into the repo-memory
file for this workspace** so the next assistant doesn't repeat the
investigation.
