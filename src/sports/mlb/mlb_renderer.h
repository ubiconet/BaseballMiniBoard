#pragma once

#include <Arduino.h>

#include "mlb_snapshot.h"

// MLB renderer — hardware outputs only. This build has no TFT, so the
// renderer drives the MAX7219 score matrices and the balls/strikes/outs
// count LEDs straight from POD snapshots: no frame buffer, no dirty-rect
// tracking, nothing to push. Cross-core data plumbing (snapshot channels,
// active gamePk, fetch diagnostics) lives in mlb_state.h; the firmware
// update indicator lives in common/ui/ota_indicator.

// Live game: away/home runs on the score matrices, balls/strikes/outs on
// the count LEDs (cleared between half innings). Skips the bit-banged
// matrix bus when nothing changed since the last call.
void renderLinescore(const LinescoreSnapshot& linescore);

// Waiting mode: count LEDs dark and the score matrices released to the
// idle clock (updateMax7219Clock in sport::tick repaints them).
void renderWaiting(const int preferredTeamIds[3]);

// Writes the current matrix + discrete LED state to Serial (DBG builds).
void logLiveDisplayState(const LinescoreSnapshot& linescore, int gamePk);
void logWaitingDisplayState(const int preferredTeamIds[3]);
