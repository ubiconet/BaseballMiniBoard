#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Fetch today's schedule JSON from statsapi.mlb.com (live games only are
// used: gamePk + team ids feed the preferred-team game selector).
bool fetchMlbSchedule(const char* dateStr, JsonDocument& doc);

// Fetch lightweight linescore JSON for a specific game (runs, balls/
// strikes/outs, inning state — everything the score matrices and count
// LEDs need).
bool fetchMlbLinescore(int gamePk, JsonDocument& doc);
