---
name: deploy-firmware
description: Build the BaseballMiniBoard firmware and publish a release to GitHub (releases/ binaries + manifest.json), then get it onto the device. Use when the user asks to build, deploy, publish, release, or push a firmware update, or to update the mini board device.
---

# Deploy a firmware release

## Preconditions

1. **Bump `FIRMWARE_VERSION`** in `src/config.h` (minor bump, e.g. `v1.0` →
   `v1.1`). The deploy target refuses to run when the version still equals
   the one in `releases/manifest.json` — devices only flash strictly newer
   versions, so an unbumped deploy is a silent no-op.
2. Build must compile cleanly:
   ```powershell
   & 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run --environment esp32-s3-devkitc-1
   ```
3. The repo must have a GitHub `origin` remote
   (`ubiconet/BaseballMiniBoard`) — the deploy push publishes the release.
   The OTA URLs in `src/config.h` already point at that repo.

## Publish

4. Commit the source changes first (the deploy step only commits
   `releases/`):
   ```powershell
   git add -A
   git commit -m "<describe the change>; v1.1"
   ```
5. Run the deploy — this builds, writes
   `releases/baseball_miniboard_latest.bin` (overwritten),
   `releases/baseball_miniboard_<version>.bin` (permanent archive), and
   `releases/manifest.json` (`{"version","file","url"}`), then commits
   `releases/` and pushes to GitHub:
   ```powershell
   & 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run --environment esp32-s3-devkitc-1 --target deploy
   ```
   The push is what publishes the update. If the guard aborts, bump the
   version (step 1) and rerun. If `git push` fails, the release is
   committed locally only — fix the error and `git push` manually.
6. GitHub's raw CDN caches the manifest ~5 minutes. Verify it flipped:
   ```powershell
   curl.exe -s https://raw.githubusercontent.com/ubiconet/BaseballMiniBoard/main/releases/manifest.json
   ```

## Getting the update onto the device

The device checks the manifest automatically after boot and flashes itself
when a newer version is published — "UD" appears on the Inning matrix while
all count LEDs flash during download; do not power off.
On this network, TLS (port 443) to GitHub is frequently blocked, so when
the user wants the device updated NOW, use the portal upload instead — it
serves plain HTTP on the LAN and always works:

7. Find the device IP from the serial log (`[NET] Online: ip=...`), then
   upload the new binary:
   ```powershell
   curl.exe -m 120 -F "update=@releases/baseball_miniboard_latest.bin" http://<device-ip>/update
   ```
   (Browser equivalent: `http://<device-ip>/update`, pick the .bin,
   "Upload & Flash". The device reboots into the new firmware.)
8. Verify over serial (`[BOOT] FW=<version>` in the boot banner, schedule
   fetch lines):
   ```powershell
   & 'C:\Users\Steve\.platformio\penv\Scripts\python.exe' tools/capture_serial.py COM13 90 serial_log.txt
   ```
   (Substitute the current COM port; `pio device list` finds it.)

## Notes

- The board's USB port also works: `pio run -t upload
  --upload-port COM13` — use when the device is plugged in and the portal
  is unreachable.
- Never edit `releases/manifest.json` by hand; the deploy target owns it.
- Full background: `AGENTS.md` §2 "Releases + firmware self-update" and
  `releases/README.md`.
