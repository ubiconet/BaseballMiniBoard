#pragma once

#include <Arduino.h>

#include "common/comms/network_service.h"

// ============================================================================
// The sport contract — the ONE seam between the generic framework
// (src/common/ + src/main.cpp) and a sport implementation
// (src/sports/<sport>/).
// ============================================================================
// The app shell (main.cpp) is fully generic: it owns the boot sequence,
// firmware self-update UI, and network bring-up, then delegates everything
// sport-specific to the `sport::` namespace implemented in the selected
// sport folder. No virtual dispatch, no heap — plain linker-level functions,
// resolved once per build via the include path / src filter in
// platformio.ini.
//
// A new sport implements every function below. Anything not listed here is
// sport-internal and free to vary.

// Per-loop-pass snapshot of shell state handed to sport::tick().
struct SportTickContext {
  uint32_t nowMs;         // millis()
  bool isOnline;          // network confirmed online
  bool isProvisioning;    // device is running its setup AP
};

namespace sport {

// ---- Identity ----

// Human-facing device name, e.g. "Baseball MiniBoard" — used for portal
// branding and Serial logs.
const char* name();

// Network branding (AP SSID, base hostname, portal title) injected into the
// generic network service.
NetworkBranding branding();

// ---- Lifecycle ----

// One-time init: hardware bring-up with this sport's pins (LED matrices,
// count LEDs) and any boot tests (e.g. the count-LED sweep). Called from
// setup() before network services start.
void setup();

// Register the sport's interactive manual-control routes on the setup server.
void registerManualControlRoutes(WebServer& portal);

// Steady-state body, called every loop() pass after the update indicator
// and network display have had their chance. Owns game state, rendering,
// and hardware updates for sport content.
void tick(const SportTickContext& ctx);

}  // namespace sport
