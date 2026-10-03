---
name: deploy-firmware
description: Build the BaseballMiniBoard firmware and publish a release to GitHub (releases/ binaries + manifest.json). Use when the user asks to build, deploy, publish, release, or push a firmware update.
---

# Deploy a firmware release

## Preconditions

1. **Bump `FIRMWARE_VERSION`** in `src/config.h` (minor bump for compatible
   changes; major bump for breaking changes). The deploy target refuses to
   run when the version still equals the one in `releases/manifest.json` —
   devices only flash strictly newer versions.
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
   git add -u
   git commit -m "<describe the change>; v2.0"
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
when a newer version is published — "UD" appears on the center matrix while
all count LEDs flash during download; do not power off.

Do not install firmware on the physical device unless the user explicitly
requests it. When requested, prefer its OTA path; a manual portal upload is
available at `http://<device-ip>/update`.

## Notes

- Never edit `releases/manifest.json` by hand; the deploy target owns it.
- Full background: the "Build, version, and release" section in `AGENTS.md`
  and `releases/README.md`.
