// ============================================================================
// Generic scoreboard app shell.
// ============================================================================
// This file is sport-agnostic: it owns network bring-up, the one-shot NTP
// sync, and the firmware-update indicator, then hands every loop pass to
// the sport implementation through the contract in common/app/sport_api.h.
// Which sport is compiled in is decided by platformio.ini
// (build_src_filter + include path pointing at src/sports/<sport>).
//
// This template is headless (no TFT): the only outputs are the sport's LED
// hardware (score matrices + count LEDs), the setup portal over Wi-Fi, and
// Serial.

#include <Arduino.h>
#include <time.h>

#include "config.h"
#include "common/app/sport_api.h"
#include "common/comms/network_service.h"
#include "common/ui/ota_indicator.h"

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  // Native USB CDC enumeration grace period
  uint32_t start = millis();
  while (!Serial && (millis() - start < 3000)) {
    delay(10);
  }

  // Sport bring-up: hardware init with the sport's pins and boot tests
  // (the count-LED sweep is this build's boot sign).
  sport::setup();

  NetworkBranding branding = sport::branding();
  size_t teamOptionCount = 0;
  startNetworkServices(branding, sport::teamOptions(teamOptionCount),
                       teamOptionCount, sport::defaultPreferredTeams());
  startNetworkTask();
  sport::startDataTask();  // core-0 feed fetches

  // Boot banner — visible over Serial so a freshly uploaded firmware can
  // be confirmed at a glance. The setup portal also shows the version on
  // its Firmware Update panel.
  Serial.println();
  Serial.printf("[BOOT] FW=%s build=%s %s\n",
                FIRMWARE_VERSION, __DATE__, __TIME__);
  Serial.printf("[BOOT] heap_free=%u heap_min=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMinFreeHeap());
}

void loop() {
  // An in-progress firmware update owns the LED hardware whenever it runs
  // (progress bar across the score matrices — "do not turn off"); the
  // sport UI owns every frame after that. Network/update/game-data work
  // runs on core 0 throughout.
  if (handleOtaIndicator()) {
    return;
  }

  handleNetworkDisplay();

  // One-shot NTP sync once the network is up. The display timezone is a
  // portal setting (NVS "tz"); the factory default in src/config.h only
  // covers the very first boot, and a mid-session portal change applies
  // itself via setenv/tzset in the network service.
  static bool timeSyncRequested = false;
  if (!timeSyncRequested && isOnline()) {
    configTzTime(getTzString(), "pool.ntp.org", "time.nist.gov");
    timeSyncRequested = true;
    DBG_PRINTF("[TIME] NTP sync requested; timezone=%s\n", getTzString());
  }

  SportTickContext ctx = {millis(), isOnline(), isProvisioning()};
  sport::tick(ctx);
}
