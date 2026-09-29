#pragma once

#include <Arduino.h>

#include "mlb_snapshot.h"

// Cross-core state for the MLB app: the two feed snapshot channels, the
// active-game selector, and the schedule-fetch diagnostics printed in the
// waiting-mode status line. Writers: core-0 data task
// (publish*/bump/set-diagnostics) and the main loop (setActiveGamePk).
// Readers: render core (take*/get*).

// Copies the current snapshot into `out` only if a new generation has been
// published since the caller's last sample. Returns true when a fresh (and
// valid) payload was delivered.
bool takeLinescoreSnapshot(LinescoreSnapshot& out, uint32_t& lastGen);
bool takeScheduleSnapshot(ScheduleSnapshot& out, uint32_t& lastGen);

// Core-0 publishers: push fresh snapshots to the render core.
namespace mlb_data {
void publishLinescore(const LinescoreSnapshot& s);
void publishSchedule(const ScheduleSnapshot& s);
}

// Current live-game tracker (set by the main loop; read by the data task).
// 0 means "no game followed / waiting mode".
int  getActiveGamePk();
void setActiveGamePk(int gamePk);

// Diagnostic helpers for the waiting-mode status line. They report what
// the data task has actually published so we can tell at a glance whether
// the schedule fetch is failing or the slate is just empty.
uint32_t getScheduleFetchAttempts();          // # times the schedule fetch ran
uint32_t getScheduleFetchSuccesses();         // # times it returned true
void     bumpScheduleFetchAttempt();          // data task calls on each fetch entry
void     bumpScheduleFetchSuccess();          // data task calls on fetch success
const char* getScheduleLastError();           // short tag for the most recent failure
void        setScheduleLastError(const char* err);
const char* getScheduleLastUrl();             // short tag of the last URL attempted
void        setScheduleLastUrl(const char* url);
int         getScheduleLastHttpCode();        // last HTTP response code (-1 = never)
void        setScheduleLastHttpCode(int code);
uint32_t    getScheduleLastFetchAt();         // millis() of last fetch attempt
void        setScheduleLastFetchAt(uint32_t ms);
