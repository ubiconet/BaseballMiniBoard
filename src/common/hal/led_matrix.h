#pragma once

#include <Arduino.h>

// Dual cascaded MAX7219 8x8 LED matrix driver (bit-banged — no SPI
// peripheral). Device 0 = Away, device 1 = Home on the MLB build; the driver
// itself only knows "away matrix / home matrix" and scores. Pins are passed
// to initLedMatrix() by the sport/app layer (from its sport_config.h).

// Pass this instead of a real score to leave a matrix dark (no game active).
static const int MAX7219_SCORE_BLANK = -1;

void initLedMatrix(int dinPin, int clkPin, int csPin);
void setMax7219Scores(int awayScore, int homeScore);
// Shows or clears local time: hour on the home matrix, minutes on the away
// matrix. Refreshes once per minute; waits for NTP before first display.
void updateMax7219Clock(bool enabled);
// Forces the next updateMax7219Clock(true) call to repaint even when the
// minute hasn't changed (e.g. after leaving a live game).
void invalidateMax7219Clock();

// Temporarily draws individual pixels on both matrices. Each mask uses bits
// 0..63 in row-major order as viewed from the front; the driver restores the
// previous matrix images when the override is disabled.
void setMax7219PixelsOverride(bool enabled, uint64_t awayPixels,
                              uint64_t homePixels);

// Temporarily shows "UD" on the home matrix (device 1 / position 2),
// preserving the other matrix and restoring both previous images on exit.
void setMax7219UpdateNotice(bool enabled);

// Lights `litColumns` of the 16 matrix columns as a solid bar across both
// modules (0 = dark, 16 = all on).
void setMax7219Bar(int litColumns);

// TEST ONLY: shows "H" on the home matrix and "A" on the away matrix at boot.
void runMax7219BootTest();
