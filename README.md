# BaseballMiniBoard

ESP32-S3 firmware for a headless mini MLB scoreboard: 2× MAX7219 8×8 score
matrices and 7 balls/strikes/outs LEDs — **no TFT screen**. When a preferred
team is live, the matrices show the away/home scores and the LEDs count
balls/strikes/outs; otherwise the matrices show the local time as an idle
clock. Both out LEDs indicate that the board is offline; all three ball LEDs
indicate that it is connected and idle. At boot, the setup access point and
captive portal are available for Wi-Fi configuration and remain available
after the board connects to the configured Wi-Fi network (AP+STA mode). The board
self-updates its firmware from this repo's `releases/` folder; during an
update, all seven count LEDs flash every 250 ms and the Home matrix displays
`UD`.

Forked from the [MLBScoreboard](https://github.com/ubiconet/mlb_scoreboard)
project (v2.60) with the TFT panel and everything that existed only to feed
it removed: boot/OTA/setup screens, the linescore/waiting renderers, the
news ticker, the at-bat result card, standings, and the team/boot logo
assets. The firmware-update "do not turn off" state now shows as a progress
bar across the two matrices.

The repo is still a **sport scoreboard template**: the generic framework
(`src/common/` + `src/main.cpp`) is sport-agnostic, and everything MLB
lives in `src/sports/mlb/`. See *Starting a new sport* below.

## Architecture

```
┌─────────────────────────── core 1 (Arduino loop) ──────────────────────┐
│ main.cpp (generic shell)      sports/mlb/mlb_app.cpp + mlb_renderer   │
│  OTA indicator / NTP sync ──▶ WAITING↔LIVE state machine,            │
│                               score matrices + count LEDs            │
│                               │ takes POD snapshots                   │
└───────────────────────────────┼────────────────────────────────────────┘
                                │ SnapshotChannel<T> (lock-free, gen ctr)
┌────────────────────────── core 0 (FreeRTOS) ───────────────────────────┐
│ network_service (Wi-Fi AP/portal, P2)   mlb_data_task (feeds, P1)     │
│ ota_update (self-update via TLS)         mlb_client → http_fetch      │
└────────────────────────────────────────────────────────────────────────┘
```

Source layout and conventions: see `AGENTS.md` §1. Build/deploy: `AGENTS.md`
§2. Released binaries + `manifest.json` live in [`releases/`](releases/) —
devices self-update from them after boot.

## Building

```powershell
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run --environment esp32-s3-devkitc-1
```

Hardware notes (pins, heap/TLS constraints) are in `docs/` and `AGENTS.md`.

## Starting a new sport from this template

The framework (`src/main.cpp` + `src/common/`) never names a sport. A sport
is a folder under `src/sports/` that implements the `sport::` contract
(`src/common/app/sport_api.h`) and provides `sport_config.h`. Checklist:

1. **Copy the repo** (new GitHub repo per sport keeps OTA streams separate).
2. **Copy `src/sports/mlb` → `src/sports/<sport>`** and rewrite the sport:
   - `sport_config.h` — pins, branding (AP SSID, hostname, portal title),
     feed poll cadences.
   - `mlb_teams.*` — the league's team table `{id, abbrev, label}`.
   - `mlb_snapshot.h` — your live-game POD structs (MLB's carries runs,
     balls/strikes/outs, inning state; hockey might carry period, shots,
     power play…).
   - `mlb_client.*` — feed endpoints + JSON filters (build on
     `common/comms/http_fetcher`; keep feeds on plain HTTP, payloads small).
   - `mlb_data_task.cpp` — poll cadence + parse-to-snapshot publishing.
   - `mlb_app.cpp` / `mlb_renderer.cpp` — your state machine and LED output.
3. **Point the env at it** in `platformio.ini`: change `+<sports/mlb/>`
   to `+<sports/<sport>/>` in `build_src_filter` and `-Isrc/sports/mlb`
   to `-Isrc/sports/<sport>` in `build_flags`, then delete the old folder.
4. **Repo identity**: set `OTA_MANIFEST_URL` / `OTA_LATEST_BIN_URL` in
   `src/config.h`, and `RAW_BASE` + `LATEST_FILE` in
   `tools/release_deploy.py`, to the new repo; bump `FIRMWARE_VERSION` to
   `v1.0`.
5. Build, USB-flash, walk through the setup portal, and run a live-game
   session against your feed.

Notes for the port: NVS keys (`ssid`, `team1..3`, `show_clock`) are shared
by design so a re-purposed board keeps its Wi-Fi; team ids are league-local,
so clear/re-pick teams in the portal after re-flashing a board.
