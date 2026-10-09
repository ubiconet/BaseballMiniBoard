#include <Arduino.h>
#include <string.h>

#include "common/hal/count_leds.h"
#include "common/hal/led_matrix.h"
#include "baseball_renderer.h"

namespace {
const uint8_t INNING_DIGITS_3X5[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111},
  {0b010, 0b110, 0b010, 0b010, 0b111},
  {0b111, 0b001, 0b111, 0b100, 0b111},
  {0b111, 0b001, 0b111, 0b001, 0b111},
  {0b101, 0b101, 0b111, 0b001, 0b001},
  {0b111, 0b100, 0b111, 0b001, 0b111},
  {0b111, 0b100, 0b111, 0b101, 0b111},
  {0b111, 0b001, 0b010, 0b010, 0b010},
  {0b111, 0b101, 0b111, 0b101, 0b111},
  {0b111, 0b101, 0b111, 0b001, 0b111}
};

// Large digits for single-digit innings; same glyphs as the score matrices.
const uint8_t INNING_DIGITS_5X7[10][7] = {
  {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
  {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110},
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
  {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
  {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}
};

void setPixel(uint8_t rows[8], int row, int column) {
  rows[row] |= static_cast<uint8_t>(1U << (7 - column));
}

// Half-inning marker: a 3-pixel line down the left edge plus a corner
// pixel at the top-left (top half) or bottom-left (bottom half).
void setHalfMarker(uint8_t rows[8], bool topHalf) {
  if (topHalf) {
    for (int row = 0; row < 3; ++row) {
      setPixel(rows, row, 0);
    }
    setPixel(rows, 0, 1);
  } else {
    for (int row = 5; row < 8; ++row) {
      setPixel(rows, row, 0);
    }
    setPixel(rows, 7, 1);
  }
}

void buildInningRows(const ManualGameState& state, uint8_t rows[8]) {
  memset(rows, 0, 8);
  const int inning = state.inning;
  if (inning < 10) {
    // Centered big digit (cols 2-6); the corner marker marks the half.
    for (int row = 0; row < 7; ++row) {
      rows[row] = static_cast<uint8_t>(
          (INNING_DIGITS_5X7[inning][row] & 0x1F) << 1);
    }
  } else {
    // Above 9: two smaller digits side by side.
    const int tens = inning / 10;
    const int ones = inning % 10;
    for (int row = 0; row < 5; ++row) {
      rows[row + 1] = (INNING_DIGITS_3X5[tens][row] << 4) |
                      INNING_DIGITS_3X5[ones][row];
    }
  }
  setHalfMarker(rows, state.topHalf != 0);
}
}  // namespace

void renderManualGame(const ManualGameState& state) {
  uint8_t inningRows[8];
  buildInningRows(state, inningRows);
  setMax7219Scores(state.awayScore, state.homeScore);
  setMax7219CenterRows(inningRows);
  setCountLeds(state.balls, state.strikes, state.outs);
}
