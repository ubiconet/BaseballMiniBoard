#pragma once

#include <Arduino.h>

// Three cascaded MAX7219 8x8 LED matrices (bit-banged — no SPI peripheral).
// Device 0 = left, device 1 = center, device 2 = right. Pins are passed to
// initLedMatrix() by the sport/app layer (from its sport_config.h).

// Pass this instead of a score to leave a matrix dark.
static const int MAX7219_SCORE_BLANK = -1;

void initLedMatrix(int dinPin, int clkPin, int csPin);
void setMax7219Scores(int awayScore, int homeScore);
// Replaces the raw 8-row bitmap on the center matrix (device 1).
void setMax7219CenterRows(const uint8_t rows[8]);
// Temporarily draws individual pixels on all three matrices. Each mask uses
// bits 0..63 in row-major order as viewed from the front.
void setMax7219PixelsOverride(bool enabled, uint64_t awayPixels,
                              uint64_t centerPixels, uint64_t homePixels);

// Temporarily shows "UD" on the center matrix (device 1 / position 2).
void setMax7219UpdateNotice(bool enabled);

// Lights `litColumns` of the 24 matrix columns across all three modules.
void setMax7219Bar(int litColumns);

// TEST ONLY: shows distinct glyphs on the left, center, and right matrices.
void runMax7219BootTest();
