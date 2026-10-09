#pragma once

// ============================================================================
// Composition root + device/repo identity
// ============================================================================
// This header composes the full build configuration from three layers:
//
//   1. THIS FILE           — identity of this particular repo/device install:
//                            firmware version and self-update URLs.
//   2. common/config.h     — framework defaults shared by every scoreboard
//                            built on this template (debug gate, boot/network
//                            timing, OTA pacing).
//   3. sport_config.h      — the selected profile (pins, branding, layout
//                            constants). Resolved from the src/sports/<sport>/
//                            folder that each
//                            PlatformIO env puts on the include path — see
//                            the -I flag in platformio.ini.
//
// Every source file keeps including plain "config.h" and sees the union of
// all three, exactly like the pre-template single config.h did.

// ---- Firmware identity -----------------------------------------------------
// tools/release_deploy.py reads FIRMWARE_VERSION from THIS file to name the
// release binary, so the definition must stay here. There is no boot splash
// to draw it on — confirm the running version via the Serial boot banner
// ([BOOT] FW=...) or the setup portal's Firmware Update panel.
static const char* FIRMWARE_VERSION = "v2.4";

// ---- Firmware self-update endpoints ----------------------------------------
// `pio run -t deploy` writes the binary + manifest to releases/ in this
// GitHub repo; the device polls the manifest after boot and flashes itself
// when the version is strictly newer than FIRMWARE_VERSION.
// raw.githubusercontent.com only serves HTTPS. The updater uses one TLS
// session for the manifest and binary so the handshake is not repeated.
// TEMPLATE CHECKLIST: point these at the new repo when forking for a new
// sport (and update RAW_BASE + LATEST_FILE in tools/release_deploy.py).
// These URLs MUST NOT stay pointed at a sibling project's repo — a device
// that self-updates from the wrong stream flashes that project's firmware.
static const char* OTA_MANIFEST_URL =
    "https://raw.githubusercontent.com/ubiconet/BaseballMiniBoard/main/"
    "releases/manifest.json";
// Displayed on the setup portal so a user doing a manual update knows
// where the current binary lives. Must stay in sync with the manifest
// URL above (same folder, deployed by `pio run -t deploy`).
static const char* OTA_LATEST_BIN_URL =
    "https://raw.githubusercontent.com/ubiconet/BaseballMiniBoard/main/"
    "releases/baseball_miniboard_latest.bin";

#include "common/config.h"
#include "sport_config.h"
