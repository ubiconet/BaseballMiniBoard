// ============================================================================
// Generic scoreboard app shell.
// ============================================================================
// This file is sport-agnostic: it owns network bring-up and the firmware
// update indicator, then hands every loop pass to
// the sport implementation through the contract in common/app/sport_api.h.
// Which sport is compiled in is decided by platformio.ini
// (build_src_filter + include path pointing at src/sports/<sport>).
//
// This template is headless (no TFT): the only outputs are the sport's LED
// hardware (score matrices + count LEDs), the setup portal over Wi-Fi, and
// Serial.

#include <Arduino.h>
#include <esp_system.h>

#include "config.h"
#include "common/app/sport_api.h"
#include "common/comms/network_service.h"
#include "common/ui/display_test.h"
#include "common/ui/ota_indicator.h"

namespace {
const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}
}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  // Native USB CDC enumeration grace period
  uint32_t start = millis();
  while (!Serial && (millis() - start < 3000)) {
    delay(10);
  }

  const esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("[BOOT] reset_reason=%s (%d)\n",
                resetReasonName(resetReason),
                static_cast<int>(resetReason));

  // Sport bring-up: hardware init with the sport's pins and boot tests
  // (the count-LED sweep is this build's boot sign).
  sport::setup();

  NetworkBranding branding = sport::branding();
  startNetworkServices(branding, sport::registerManualControlRoutes);
  startNetworkTask();

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
  // OTA has priority over the interactive display test; both keep hardware
  // writes on this render core rather than the network task.
  if (handleOtaIndicator()) {
    return;
  }

  if (handleDisplayTest()) {
    return;
  }

  handleNetworkDisplay();

  SportTickContext ctx = {millis(), isOnline(), isProvisioning()};
  sport::tick(ctx);
}
