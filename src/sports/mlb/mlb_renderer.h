#pragma once

#include <Arduino.h>

#include "mlb_snapshot.h"

// MLB renderer — hardware outputs only. This build has no TFT, so the
// renderer drives the MAX7219 score matrices and the balls/strikes/outs
// count LEDs straight from POD snapshots: no frame buffer, no dirty-rect
// tracking, nothing to push. Cross-core data plumbing (snapshot channels,
// active gamePk, fetch diagnostics) lives in mlb_state.h; the firmware
// update indicator lives in common/ui/ota_indicator.

// Live game: Away, Inning, and Home matrices show score/half-inning state;
// balls/strikes/outs appear on the count LEDs (cleared between half innings).
// Skips matrix writes when their content has not changed.
void renderLinescore(const LinescoreSnapshot& linescore);

// Waiting mode: all ball LEDs indicate a connected, idle board; the outer
// matrices are released to the idle clock and the Inning matrix is blank.
void renderWaiting(const int preferredTeamIds[3]);

// Offline mode: light both out LEDs while the board has no network connection.
void renderNetworkDisconnected();

// Writes the current matrix + discrete LED state to Serial (DBG builds).
void logLiveDisplayState(const LinescoreSnapshot& linescore, int gamePk);
void logWaitingDisplayState(const int preferredTeamIds[3]);
