#include <Arduino.h>

#include "config.h"
#include "common/hal/count_leds.h"
#include "common/hal/led_matrix.h"
#include "mlb_renderer.h"
#include "mlb_state.h"

// Hardware-only renderer for the headless mini board. Everything the TFT
// panel used to carry (team cards, base diamond, inning header, at-bat
// result card, around-the-league ticker, news marquee) is out of scope —
// the POD snapshot drives the matrices and count LEDs directly, and each
// call only touches the bit-banged bus when an output actually changed.

namespace {

// Last-applied hardware state, so unchanged snapshots never re-write the
// matrices. inningState participates: a move into/between Middle/End must
// clear the count LEDs even when the counts themselves read the same.
struct RenderedState {
  int      awayScore;
  int      homeScore;
  int      currentInning;
  int      balls;
  int      strikes;
  int      outs;
  uint8_t  inningState;
};
RenderedState gRendered{};
bool gHasRendered = false;

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
const uint8_t INNING_TOP_3X5[5] = {0b111, 0b010, 0b010, 0b010, 0b010};
const uint8_t INNING_BOTTOM_3X5[5] = {0b110, 0b101, 0b110, 0b101, 0b110};

void setInningPixel(uint8_t rows[8], int row, int column) {
  rows[row] |= static_cast<uint8_t>(1U << (7 - column));
}

void buildInningRows(int inning, uint8_t inningState, uint8_t rows[8]) {
  memset(rows, 0, 8);
  if (inning <= 0) return;
  if (inning > 99) inning = 99;

  if (inning < 10 && (inningState == 0 || inningState == 1)) {
    const uint8_t* half =
        inningState == 0 ? INNING_TOP_3X5 : INNING_BOTTOM_3X5;
    for (int row = 0; row < 5; ++row) {
      rows[row + 1] = (half[row] << 4) |
                      (INNING_DIGITS_3X5[inning][row] & 0x07);
    }
  } else if (inning < 10) {
    for (int row = 0; row < 5; ++row) {
      rows[row + 1] =
          (INNING_DIGITS_3X5[inning][row] & 0x07) << 3;
    }
  } else {
    int tens = inning / 10;
    int ones = inning % 10;
    for (int row = 0; row < 5; ++row) {
      rows[row + 1] = (INNING_DIGITS_3X5[tens][row] << 4) |
                      INNING_DIGITS_3X5[ones][row];
    }
    if (inningState == 0) setInningPixel(rows, 0, 3);
    else if (inningState == 1) setInningPixel(rows, 7, 3);
  }
}

}  // namespace

void renderLinescore(const LinescoreSnapshot& ls) {
  bool betweenHalfInnings = (ls.inningState == 2) || (ls.inningState == 3);

  bool scoreChanged = !gHasRendered ||
      ls.awayScore != gRendered.awayScore || ls.homeScore != gRendered.homeScore;
  bool inningChanged = !gHasRendered ||
      ls.currentInning != gRendered.currentInning ||
      ls.inningState != gRendered.inningState;
  bool countChanged = !gHasRendered ||
      ls.balls != gRendered.balls || ls.strikes != gRendered.strikes ||
      ls.outs != gRendered.outs || ls.inningState != gRendered.inningState;

  if (scoreChanged) {
    setMax7219Scores(ls.awayScore, ls.homeScore);
  }
  if (inningChanged) {
    uint8_t inningRows[8];
    buildInningRows(ls.currentInning, ls.inningState, inningRows);
    setMax7219CenterRows(inningRows);
  }
  if (countChanged) {
    if (betweenHalfInnings) setCountLeds(0, 0, 0);
    else                    setCountLeds(ls.balls, ls.strikes, ls.outs);
  }

  gRendered.awayScore  = ls.awayScore;
  gRendered.homeScore  = ls.homeScore;
  gRendered.currentInning = ls.currentInning;
  gRendered.balls      = ls.balls;
  gRendered.strikes    = ls.strikes;
  gRendered.outs       = ls.outs;
  gRendered.inningState = ls.inningState;
  gHasRendered = true;
}

void renderWaiting(const int preferredTeamIds[3]) {
  (void)preferredTeamIds;
  setCountLeds(3, 0, 0);
  const uint8_t blankRows[8] = {0};
  setMax7219CenterRows(blankRows);
  // Release the matrices to the idle clock and force its next pass to
  // repaint, so stale game scores can't linger through waiting mode.
  invalidateMax7219Clock();
  // The next live frame must repaint even when its first snapshot happens
  // to equal the last one rendered before the game ended.
  gHasRendered = false;
}

void renderNetworkDisconnected() {
  setCountLeds(0, 0, 2);
}

void logLiveDisplayState(const LinescoreSnapshot& ls, int gamePk) {
  const char* inningStateName = "Unknown";
  switch (ls.inningState) {
    case 0: inningStateName = "Top";    break;
    case 1: inningStateName = "Bottom"; break;
    case 2: inningStateName = "Middle"; break;
    case 3: inningStateName = "End";    break;
  }
  DBG_PRINTF(
    "[DISPLAY] state=LIVE_GAME gamePk=%d | %s %d | matrices: away=%d "
    "inning=%d home=%d | count LEDs: balls=%u strikes=%u outs=%u\n",
    gamePk, inningStateName, ls.currentInning,
    ls.awayScore, ls.currentInning, ls.homeScore,
    ls.balls, ls.strikes, ls.outs);
}

void logWaitingDisplayState(const int preferredTeamIds[3]) {
  DBG_PRINTF(
    "[DISPLAY] state=WAITING | matrices: idle clock | count LEDs: "
    "balls=0 strikes=0 outs=0 | priorities=[%d,%d,%d] | sched: att=%lu "
    "ok=%lu err=%s\n",
    preferredTeamIds[0], preferredTeamIds[1], preferredTeamIds[2],
    (unsigned long)getScheduleFetchAttempts(),
    (unsigned long)getScheduleFetchSuccesses(),
    getScheduleLastError());
}
