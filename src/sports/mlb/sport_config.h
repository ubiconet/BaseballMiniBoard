#pragma once

#include <Arduino.h>   // uint32_t & friends — config headers are self-sufficient

// ============================================================================
// MLB mini board — sport + device profile.
// ============================================================================
// This file (named sport_config.h in every sport folder) is what the generic
// framework in src/common/ composes in via the include path each PlatformIO
// env sets up. It carries everything that changes between sports AND between
// physical builds: display pins, branding strings, and feed poll cadences.
//
// Starting a new sport from this template: copy src/sports/mlb to
// src/sports/<sport>, edit this file, and point the env's -I flag and
// build_src_filter at the new folder (see README "new sport" checklist).
//
// This build has no TFT: the only displays are the two MAX7219 score
// matrices and the seven discrete count LEDs.

// ---- MAX7219 8x8 LED Matrix displays (2 cascaded modules: 0=Away, 1=Home) --
static const int MAX7219_DIN_PIN = 14;
static const int MAX7219_CLK_PIN = 8;
static const int MAX7219_CS_PIN = 16;

// ---- Discrete Count LEDs (GPIO pins; balls/strikes/outs) --------------------
static const int BALL_3_PIN = 1;
static const int BALL_2_PIN = 2;
static const int BALL_1_PIN = 3;
static const int STRIKE_2_PIN = 4;
static const int STRIKE_1_PIN = 5;
static const int OUT_2_PIN = 6;
static const int OUT_1_PIN = 7;

// ---- Branding (AP network name, hostname, portal title) ---------------------
static const char* NETWORK_AP_SSID = "BASEBALL_MINIBOARD";
static const char* NETWORK_HOSTNAME = "baseball-miniboard";

// ---- MLB feed polling --------------------------------------------------------
static const uint32_t MLB_LIVE_POLL_INTERVAL_MS = 5000;  // 5 second live linescore tick
static const uint32_t MLB_SCHEDULE_POLL_INTERVAL_MS = 60000; // Detect followed-game start/end within 1 min
static const uint32_t MLB_SCHEDULE_RETRY_MS = 15000;   // Base retry while the last schedule fetch failed (flaky Wi-Fi)
static const uint32_t MLB_RETRY_BACKOFF_MAX_MS = 120000; // Exponential backoff cap for failed fetch retries.
// After the first couple of connections following boot, new TLS connections
// start failing instantly (start_ssl_client: -1) with the radio still
// associated and heap healthy — consistent with the AP's flood protection
// and/or leaked lwIP PCBs from failed handshakes. Backing off exponentially
// (per ADR-0002) instead of retrying every 15 s lets those windows expire.
