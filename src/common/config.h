#pragma once

#include <Arduino.h>   // uint32_t & friends — config headers are self-sufficient

// ============================================================================
// Framework defaults — shared by every scoreboard built on this template.
// Nothing in here knows about a specific sport, feed, or display layout;
// sport/pin/branding constants live in the selected sport's sport_config.h.
// ============================================================================

// Serial
static const uint32_t SERIAL_BAUD_RATE = 115200;

// Compile-time debug log gate. Set to 0 in production builds to drop the
// per-tick [DISPLAY]/[API CALL] printf noise (a Serial.printf at 115200 baud
// stalls the loop ~25 ms — measurable against the 5s poll cadence). Enable by
// uncommenting or passing -DSB_DEBUG=1 in build_flags during development.
#ifndef SB_DEBUG
#define SB_DEBUG 0   // production default; pass -DSB_DEBUG=1 in build_flags to diagnose
#endif
#if SB_DEBUG
#define DBG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
#define DBG_PRINTF(fmt, ...) do {} while (0)
#endif

// Network portal login + dev-flashing password (device-level, not sport).
static const char* NETWORK_AP_PASSWORD = "score1234";
static const char* NETWORK_PORTAL_PASSWORD = "score";
// Network (Arduino) OTA: used by `pio run -t upload --upload-port <ip>` during development.
static const char* NETWORK_OTA_PASSWORD = "score1234";
// Connectivity probe: any HTTP response from this anchor means the uplink works.
static const char* NETWORK_PROBE_ANCHOR_URL = "http://connectivitycheck.gstatic.com/generate_204";
static const uint32_t NETWORK_CONNECT_AND_PROBE_TIMEOUT_MS = 30000; // assoc + internet probe budget
static const uint32_t NETWORK_PROBE_INTERVAL_MS = 5000;
static const uint32_t NETWORK_RECONNECT_GRACE_MS = 15000; // sustained drop before re-provisioning
static const uint32_t NETWORK_RECONNECT_RETRY_MS = 5000;
static const uint32_t NETWORK_DEBUG_INTERVAL_MS = 10000;
static const uint32_t NETWORK_SETUP_SCREEN_MS = 0;         // no online screen; release scoreboard immediately
static const uint32_t NETWORK_SCAN_REFRESH_MS = 30000;    // portal scan-list cache age
static const uint32_t NETWORK_PROVISIONING_RETRY_MS = 60000; // retry saved Wi-Fi after this long in AP mode
// While anyone is using the setup portal (any HTTP request refreshes this
// window), feed fetching pauses entirely so the portal loads fast even on
// this device's marginal Wi-Fi.
static const uint32_t PORTAL_ACTIVITY_WINDOW_MS = 120000;
// Formerly delayed the (now removed) Wi-Fi connecting screen at boot. Kept
// at zero so nothing delays going online.
static const uint32_t NETWORK_FIRST_CONNECT_MIN_MS = 0;

// Firmware self-update pacing (endpoints live in src/config.h — they are
// repo identity, not framework policy).
static const uint32_t OTA_FIRST_CHECK_AFTER_ONLINE_MS = 5000; // runs behind the boot screens; needs the pristine boot heap
static const uint32_t OTA_BOOT_GATE_TIMEOUT_MS = 25000; // feeds must not starve behind failed TLS attempts
static const uint32_t OTA_CHECK_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL; // recheck
static const uint32_t OTA_CHECK_RETRY_MS = 10UL * 60UL * 1000UL;  // retry failed checks — keep after a flaky network
static const uint32_t OTA_DOWNLOAD_STALL_MS = 30000;  // abort a download with no progress
