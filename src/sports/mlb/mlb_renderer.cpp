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
  int      balls;
  int      strikes;
  int      outs;
  uint8_t  inningState;
};
RenderedState gRendered{};
bool gHasRendered = false;

}  // namespace

void renderLinescore(const LinescoreSnapshot& ls) {
  bool betweenHalfInnings = (ls.inningState == 2) || (ls.inningState == 3);

  bool scoreChanged = !gHasRendered ||
      ls.awayScore != gRendered.awayScore || ls.homeScore != gRendered.homeScore;
  bool countChanged = !gHasRendered ||
      ls.balls != gRendered.balls || ls.strikes != gRendered.strikes ||
      ls.outs != gRendered.outs || ls.inningState != gRendered.inningState;

  if (scoreChanged) {
    setMax7219Scores(ls.awayScore, ls.homeScore);
  }
  if (countChanged) {
    if (betweenHalfInnings) setCountLeds(0, 0, 0);
    else                    setCountLeds(ls.balls, ls.strikes, ls.outs);
  }

  gRendered.awayScore  = ls.awayScore;
  gRendered.homeScore  = ls.homeScore;
  gRendered.balls      = ls.balls;
  gRendered.strikes    = ls.strikes;
  gRendered.outs       = ls.outs;
  gRendered.inningState = ls.inningState;
  gHasRendered = true;
}

void renderWaiting(const int preferredTeamIds[3]) {
  (void)preferredTeamIds;
  setCountLeds(0, 0, 0);
  // Release the matrices to the idle clock and force its next pass to
  // repaint, so stale game scores can't linger through waiting mode.
  invalidateMax7219Clock();
  // The next live frame must repaint even when its first snapshot happens
  // to equal the last one rendered before the game ended.
  gHasRendered = false;
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
    "[DISPLAY] state=LIVE_GAME gamePk=%d | %s %d | matrices: home=%d "
    "away=%d | count LEDs: balls=%u strikes=%u outs=%u\n",
    gamePk, inningStateName, ls.currentInning,
    ls.homeScore, ls.awayScore, ls.balls, ls.strikes, ls.outs);
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
