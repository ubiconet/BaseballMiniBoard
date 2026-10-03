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
};

void initializeManualGameState();
bool takeManualGameState(ManualGameState& state, uint32_t& lastGeneration);
void registerManualControlRoutes(WebServer& portal);
