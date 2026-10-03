#pragma once

#include <Arduino.h>

// Seven discrete LEDs wired as three bar-graph counters of 3/2/2 lamps.
// The profile supplies the pin map and the current count for each group.
//
// Pin order for initCountLeds():
//   pins[0..2] = counter A, least-significant lamp first (balls 1..3)
//   pins[3..4] = counter B (strikes 1..2)
//   pins[5..6] = counter C (outs 1..2)

void initCountLeds(const int pins[7]);
// Lights the first `countA/countB/countC` lamps of each counter; values are
// clamped by construction (>= comparisons), extra lamps stay dark.
void setCountLeds(uint8_t countA, uint8_t countB, uint8_t countC);

// Temporarily override individual LEDs without losing the most recently
// requested counter values; disabling the override restores that display.
// Bits 0..6 follow initCountLeds() pin order.
void setCountLedsOverride(bool enabled, uint8_t ledMask);

// TEST ONLY: cycles each LED on for 0.5 s, one at a time, then clears.
void runCountLedTestLoop();
