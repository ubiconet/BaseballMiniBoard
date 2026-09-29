#pragma once

#include <Arduino.h>

// Shared "snapshot" structs written by the core-0 MLB data task and read by
// the core-1 render loop. All fields are plain POD so the writer never has
// to hold a lock while reading; the render loop reads them atomically (single
// aligned struct copy). A small generation counter signals when a new payload
// has been published since the last read.
//
// This replaces the old pattern where every MLB API call (linescore,
// schedule) blocked inside loop() with 3-5s HTTP timeouts. Offloading them
// to core 0 means core 1's render loop only ever touches the LED matrices
// and count LEDs, so display timing is no longer hostage to network latency.
//
// The structs carry only what this headless board can show: runs, counts,
// and inning state. TFT-era fields (team names, batter/pitcher, base
// runners, other-game scores/inning ordinals) were dropped along with the
// screen that displayed them.

struct LinescoreSnapshot {
  int gamePk;
  int awayScore;
  int homeScore;
  int balls;
  int strikes;
  int outs;
  int currentInning;
  uint8_t inningState; // 0=Top, 1=Bottom, 2=Middle, 3=End, 4=other
  bool valid;
};

// One live game on today's slate. The schedule snapshot is just a list of
// these; game selection happens on the render core against the preferred
// teams.
struct OtherGameLite {
  int gamePk;
  int awayTeamId;
  int homeTeamId;
};

struct ScheduleSnapshot {
  OtherGameLite others[9];
  size_t otherCount;
  bool valid;
};

// Core-0 MLB data task lifecycle.
void startMlbDataTask();

// Publishers used by the core-0 data task to push fresh snapshots to the
// render core. Defined in mlb_state.cpp at file scope.
namespace mlb_data {
void publishLinescore(const LinescoreSnapshot& s);
void publishSchedule(const ScheduleSnapshot& s);
}
