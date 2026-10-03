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
const uint8_t HALF_TOP_3X5[5] = {0b111, 0b010, 0b010, 0b010, 0b010};
const uint8_t HALF_BOTTOM_3X5[5] = {0b110, 0b101, 0b110, 0b101, 0b110};

void setPixel(uint8_t rows[8], int row, int column) {
  rows[row] |= static_cast<uint8_t>(1U << (7 - column));
}

void buildInningRows(const ManualGameState& state, uint8_t rows[8]) {
  memset(rows, 0, 8);
  const int inning = state.inning;
  if (inning < 10) {
    const uint8_t* half = state.topHalf ? HALF_TOP_3X5 : HALF_BOTTOM_3X5;
    for (int row = 0; row < 5; ++row) {
      rows[row + 1] = (half[row] << 4) |
                      (INNING_DIGITS_3X5[inning][row] & 0x07);
    }
    return;
  }

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
