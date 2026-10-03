#include <Arduino.h>

#include "config.h"
#include "common/app/sport_api.h"
#include "common/comms/network_service.h"
#include "common/hal/count_leds.h"
#include "common/hal/led_matrix.h"
#include "mlb_renderer.h"
#include "mlb_snapshot.h"
#include "mlb_state.h"
#include "mlb_teams.h"

// MLB app: the WAITING/LIVE state machine and sport:: contract
// implementation (see common/app/sport_api.h). The generic shell in
// src/main.cpp drives boot/network/update; everything here is what makes
// this build an MLB mini board (score matrices + count LEDs, no TFT).

namespace {

enum class ScoreboardState {
  WAITING,
  LIVE_GAME
};
ScoreboardState scoreboardState = ScoreboardState::WAITING;
const char* const MLB_COUNT_LED_LABELS[7] = {
    "Ball 1", "Ball 2", "Ball 3", "Strike 1", "Strike 2", "Out 1", "Out 2"};
const char* const MLB_MATRIX_LABELS[3] = {
    "Away score", "Inning", "Home score"};

// "Last seen" generation counters for the snapshots we consume.
uint32_t sLastLinescoreGen = 0;
uint32_t sLastScheduleGen  = 0;
uint32_t sLastScheduleRenderAt = 0;
bool sNetworkStateRendered = false;
bool sLastNetworkOnline = false;

// The data task can't decide on its own which game the user wants to follow
// (that depends on the preferred-team list, which lives in NVS on core 1).
// We make that decision here, set activeGamePk via setActiveGamePk(), and the
// data task reads it on the next iteration. Until core-0 publishes its first
// schedule snapshot we stay in WAITING (the data task publishes a valid (but
// possibly empty) schedule within ~5 s).

// Pick the gamePk of the highest-priority preferred team currently live,
// straight from the other-games snapshot. This must not depend on any
// previously-fetched linescore: the data task only polls a linescore once a
// gamePk is selected here, so deriving the selection FROM a linescore can
// never bootstrap out of waiting mode (the bug this replaces).
int selectLiveGamePkForTeams(const ScheduleSnapshot& sch,
                             const int preferredTeams[3]) {
  for (int p = 0; p < 3; ++p) {
    int teamId = preferredTeams[p];
    if (teamId == 0) continue;
    for (size_t i = 0; i < sch.otherCount; ++i) {
      if (sch.others[i].awayTeamId == teamId ||
          sch.others[i].homeTeamId == teamId) {
        return sch.others[i].gamePk;
      }
    }
  }
  return 0;
}

}  // namespace

namespace sport {

const char* name() { return "Baseball MiniBoard"; }

NetworkBranding branding() {
  return NetworkBranding{name(), NETWORK_AP_SSID, NETWORK_HOSTNAME,
                         MLB_COUNT_LED_LABELS, MLB_MATRIX_LABELS};
}

const NetworkTeamOption* teamOptions(size_t& count) {
  return mlbTeamOptions(count);
}

const int* defaultPreferredTeams() { return MLB_DEFAULT_PREFERRED_TEAMS; }

void setup() {
  // Initialize the hardware: discrete count LEDs + MAX7219 matrices. This
  // build has no TFT — the matrices and LEDs are the whole display.
  const int countLedPins[7] = {BALL_1_PIN, BALL_2_PIN, BALL_3_PIN,
                               STRIKE_1_PIN, STRIKE_2_PIN, OUT_1_PIN,
                               OUT_2_PIN};
  initCountLeds(countLedPins);
  initLedMatrix(MAX7219_DIN_PIN, MAX7219_CLK_PIN, MAX7219_CS_PIN);

  // TEST ONLY: shows A/2/H on the Away, Inning, and Home matrices.
  //runMax7219BootTest();

  // TEST ONLY: cycles ball/strike/out LEDs one at a time — the headless
  // boot sign; leave enabled during hardware validation.
  runCountLedTestLoop();

  Serial.println("[MAIN] Baseball MiniBoard ready");
}

void startDataTask() {
  startMlbDataTask();  // core-0 linescore/schedule fetches
}

void tick(const SportTickContext& ctx) {
  bool networkStateChanged = !sNetworkStateRendered ||
                             ctx.isOnline != sLastNetworkOnline;
  bool justCameOnline = ctx.isOnline && networkStateChanged;
  bool justWentOffline = !ctx.isOnline && networkStateChanged;
  sNetworkStateRendered = true;
  sLastNetworkOnline = ctx.isOnline;

  if (consumeScoreboardRelease()) {
    int preferredTeams[3];
    getPreferredTeamIds(preferredTeams);
    scoreboardState = ScoreboardState::WAITING;
    setActiveGamePk(0);
    renderWaiting(preferredTeams);
    logWaitingDisplayState(preferredTeams);
  }

  if (!ctx.isOnline) {
    if (justWentOffline) {
      renderNetworkDisconnected();
    }
    return;
  }

  uint32_t now = ctx.nowMs;
  int preferredTeams[3];
  getPreferredTeamIds(preferredTeams);

  // Tick the scoreboard clock once per loop when idle (no live game).
  if (scoreboardState == ScoreboardState::WAITING) {
    updateMax7219Clock(isClockDisplayEnabled());
  }

  // ---- Consume new linescore snapshot from core 0 ----
  LinescoreSnapshot ls{};
  bool newLinescore = takeLinescoreSnapshot(ls, sLastLinescoreGen);

  // ---- Consume new schedule snapshot ----
  ScheduleSnapshot sch{};
  bool newSchedule = takeScheduleSnapshot(sch, sLastScheduleGen);

  // ---- Decide which game (if any) the board should follow ----
  //
  // The data task on core 0 polls linescore for whatever gamePk we tell it.
  // We pick the highest-priority preferred team that has a live game on
  // today's slate. The "other games" list inside ScheduleSnapshot carries
  // the away/home team ids; when one matches a preferred team we know there's
  // a live game for them.
  if (newSchedule && sch.valid) {
    int newGamePk = selectLiveGamePkForTeams(sch, preferredTeams);
    if (newGamePk > 0) {
      setActiveGamePk(newGamePk);
      scoreboardState = ScoreboardState::LIVE_GAME;
    }
  }

  // Render based on state.
  if (scoreboardState == ScoreboardState::WAITING) {
    // First entry to WAITING (or schedule refresh): re-arm the idle clock.
    if (justCameOnline || sLastScheduleRenderAt == 0 || newSchedule) {
      sLastScheduleRenderAt = now;
      renderWaiting(preferredTeams);
      logWaitingDisplayState(preferredTeams);
    }
  } else {
    // LIVE_GAME: each fresh linescore drives the matrices and count LEDs.
    if (newLinescore && ls.valid) {
      renderLinescore(ls);
      logLiveDisplayState(ls, ls.gamePk);
    }
  }
}

}  // namespace sport
