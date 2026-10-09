#pragma once

#include <Arduino.h>

class WebServer;

struct ManualGameState {
  uint8_t valid;
  uint8_t homeScore;
  uint8_t awayScore;
  uint8_t inning;
  uint8_t topHalf;
  uint8_t balls;
  uint8_t strikes;
  uint8_t outs;
  uint8_t gameLogic;  // 1 = Ball/Strike/Out buttons follow baseball rules
};

void initializeManualGameState();
bool takeManualGameState(ManualGameState& state, uint32_t& lastGeneration);
void registerManualControlRoutes(WebServer& portal);
