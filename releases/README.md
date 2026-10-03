# Releases

Firmware binaries + update manifest, produced and published by one command:

```powershell
& 'C:\Users\Steve\.platformio\penv\Scripts\platformio.exe' run `
    --environment esp32-s3-devkitc-1 --target deploy
```

The deploy target:

1. Builds the firmware.
2. Refuses to run if `FIRMWARE_VERSION` in `src/config.h` still matches
   the version in `manifest.json` — bump the version first, or devices
   will ignore the build.
3. Writes `baseball_miniboard_latest.bin` (always overwritten), a
   permanent `baseball_miniboard_<version>.bin` archive, and
   `manifest.json` (`version` / `file` / `url`).
4. Commits `releases/` and pushes to GitHub, which is what actually
   publishes the update — the board polls
   `raw.githubusercontent.com/ubiconet/BaseballMiniBoard/main/releases/manifest.json`
   after boot and flashes itself when the version is newer.

Note: GitHub's raw CDN caches the manifest for ~5 minutes after a push, so
a device may see the previous manifest for a few minutes after a deploy.

## Manual update (when the device can't reach GitHub over TLS)

Some networks block/drop port 443 from the board, which silently defeats
the auto-update. The device's built-in web portal has an uploader that
works over plain HTTP on your LAN — from a browser:

1. Open `http://<device-ip>/update` (the IP is printed on the
   `[NET] Online: ip=...` serial line; mDNS `baseball-miniboard-<mac>.local`
   also works when discovery is healthy).
2. Choose `releases/baseball_miniboard_latest.bin`.
3. Click **Upload & Flash** — the board reboots into the new firmware
   ("UD" on the Inning matrix while the count LEDs flash during download).

Or from PowerShell:

```powershell
curl.exe -F "update=@releases\baseball_miniboard_latest.bin" http://<device-ip>/update
```

The auto-update keeps running in the background (attempts at boot, then
every 10 minutes) and will catch up on its own whenever the network allows
the TLS connection through.
