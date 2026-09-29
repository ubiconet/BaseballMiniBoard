#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "config.h"
#include "common/comms/http_fetcher.h"
#include "mlb_client.h"
#include "mlb_state.h"

// Feed endpoints + JSON filters for the MLB build. Transport (shared
// keep-alive session, buffered body reader, filtered parse) lives in
// common/comms/http_fetcher; the plain-HTTP rationale is documented there.
namespace {

// ArduinoJson filter mask for the schedule payload: keeps just the per-game
// gamePk, live/final state, and away/home team ids the preferred-team game
// selector needs, dropping everything else.
void buildScheduleFilter(JsonDocument& filter) {
  filter["dates"][0]["games"][0]["gamePk"] = true;
  filter["dates"][0]["games"][0]["status"]["abstractGameState"] = true;
  filter["dates"][0]["games"][0]["teams"]["away"]["team"]["id"] = true;
  filter["dates"][0]["games"][0]["teams"]["home"]["team"]["id"] = true;
}
} // namespace

bool fetchMlbSchedule(const char* dateStr, JsonDocument& doc) {
  // A null/empty date asks the server for today's slate (US game-day), so the
  // device never needs its own clock.
  String url = "http://statsapi.mlb.com/api/v1/schedule?sportId=1"
               "&fields=dates,date,games,gamePk,status,abstractGameState,"
               "teams,away,team,id,home,team,id";
  if (dateStr != nullptr && dateStr[0] != '\0') {
    url += "&date=";
    url += dateStr;
  }

  setScheduleLastUrl(url.c_str());
  setScheduleLastFetchAt(millis());

  doc.clear();
  int httpCode = http_fetch::get(url, 10000);
  http_fetch::logCall("schedule", httpCode);
  setScheduleLastHttpCode(httpCode);

  if (httpCode == HTTP_CODE_OK) {
    JsonDocument filter;
    buildScheduleFilter(filter);
    DeserializationError error = http_fetch::parseBody(doc, &filter);
    if (!error) {
      return true;
    } else {
      DBG_PRINTF("[MLB] Schedule JSON parse error: %s\n", error.c_str());
      http_fetch::closeSession();
      return false;
    }
  }
  DBG_PRINTF("[MLB] Schedule HTTP error: %d\n", httpCode);
  http_fetch::closeSession();
  return false;
}

bool fetchMlbLinescore(int gamePk, JsonDocument& doc) {
  String url = "http://statsapi.mlb.com/api/v1/game/";
  url += String(gamePk);
  url += "/linescore";

  // http_fetch::get() already retries once on a fresh connection; two rounds
  // total is enough and keeps the per-poll latency bounded.
  for (int attempt = 1; attempt <= 2; attempt++) {
    int httpCode = http_fetch::get(url, 4000);
    http_fetch::logCall("linescore", httpCode);

    if (httpCode == HTTP_CODE_OK) {
      doc.clear();
      // Linescore bodies are tiny (~1.5 KB): read them via the exact-length
      // body reader so the keep-alive connection stays clean, then parse
      // the whole thing (no filter).
      DeserializationError error = http_fetch::parseBody(doc, nullptr);
      if (!error) {
        return true;
      }
      DBG_PRINTF("[MLB] Linescore JSON parse error (attempt %d/2): %s\n",
                    attempt, error.c_str());
      http_fetch::closeSession();
    } else {
      DBG_PRINTF("[MLB] Linescore HTTP error (attempt %d/2): %d\n",
                 attempt, httpCode);
      http_fetch::closeSession();
    }
  }

  DBG_PRINTF("[MLB] Linescore failed after 2 attempts\n");
  return false;
}
