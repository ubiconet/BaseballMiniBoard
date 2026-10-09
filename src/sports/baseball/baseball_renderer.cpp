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

// Half-inning arrows (3 wide, 7 tall) shown left of the inning number.
const uint8_t ARROW_UP_3X7[7] = {
  0b010, 0b111, 0b010, 0b010, 0b010, 0b010, 0b010
};
const uint8_t ARROW_DOWN_3X7[7] = {
  0b010, 0b010, 0b010, 0b010, 0b010, 0b111, 0b010
};

void setPixel(uint8_t rows[8], int row, int column) {
  rows[row] |= static_cast<uint8_t>(1U << (7 - column));
}

void buildInningRows(const ManualGameState& state, uint8_t rows[8]) {
  memset(rows, 0, 8);
  const int inning = state.inning;
  if (inning < 10) {
    // Big digit (cols 3-7) beside a half-inning arrow (cols 0-2).
    const uint8_t* arrow = state.topHalf ? ARROW_UP_3X7 : ARROW_DOWN_3X7;
    for (int row = 0; row < 7; ++row) {
      rows[row] = static_cast<uint8_t>(arrow[row] << 5) |
                  (INNING_DIGITS_5X7[inning][row] & 0x1F);
    }
    return;
  }

  // Two digits leave no room for an arrow; a corner dot marks the half.
  const int tens = inning / 10;
  const int ones = inning % 10;
  for (int row = 0; row < 5; ++row) {
    rows[row + 1] = (INNING_DIGITS_3X5[tens][row] << 4) |
                    INNING_DIGITS_3X5[ones][row];
  }
  setPixel(rows, state.topHalf ? 0 : 7, 3);
}
}  // namespace

void renderManualGame(const ManualGameState& state) {
  uint8_t inningRows[8];
  buildInningRows(state, inningRows);
  setMax7219Scores(state.awayScore, state.homeScore);
  setMax7219CenterRows(inningRows);
  setCountLeds(state.balls, state.strikes, state.outs);
}
