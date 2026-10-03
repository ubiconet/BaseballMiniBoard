#include <Arduino.h>

#include "config.h"
#include "common/app/sport_api.h"
#include "common/comms/network_service.h"
#include "common/hal/count_leds.h"
#include "common/hal/led_matrix.h"
#include "baseball_renderer.h"
#include "manual_control.h"

namespace {
const char* const COUNT_LED_LABELS[7] = {
    "Ball 1", "Ball 2", "Ball 3", "Strike 1", "Strike 2", "Out 1", "Out 2"};
const char* const MATRIX_LABELS[3] = {
    "Away score", "Inning", "Home score"};
uint32_t lastManualGeneration = 0;
}  // namespace

namespace sport {

const char* name() {
  return "Baseball MiniBoard";
}

NetworkBranding branding() {
  return NetworkBranding{name(), NETWORK_AP_SSID, NETWORK_HOSTNAME,
                         COUNT_LED_LABELS, MATRIX_LABELS};
}

void registerManualControlRoutes(WebServer& portal) {
  ::registerManualControlRoutes(portal);
}

void setup() {
  const int countLedPins[7] = {BALL_1_PIN, BALL_2_PIN, BALL_3_PIN,
                               STRIKE_1_PIN, STRIKE_2_PIN, OUT_1_PIN,
                               OUT_2_PIN};
  initCountLeds(countLedPins);
  initLedMatrix(MAX7219_DIN_PIN, MAX7219_CLK_PIN, MAX7219_CS_PIN);
  initializeManualGameState();
  runCountLedTestLoop();
  Serial.println("[MAIN] Baseball MiniBoard ready for manual scoring");
}

void tick(const SportTickContext& ctx) {
  (void)ctx;
  ManualGameState state{};
  if (takeManualGameState(state, lastManualGeneration)) {
    renderManualGame(state);
  }
}

}  // namespace sport
