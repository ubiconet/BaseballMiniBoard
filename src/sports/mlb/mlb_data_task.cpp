#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "common/comms/http_fetcher.h"
#include "common/comms/network_service.h"
#include "common/comms/ota_update.h"
#include "mlb_client.h"
#include "mlb_snapshot.h"
#include "mlb_state.h"

namespace {

// JSON workspace on core 0. Each API routine clears it before beginning a
// subsequent HTTP request, so a previous response does not crowd the
// transport buffers.
JsonDocument mlbDoc;

uint8_t classifyInningState(const char* state) {
  if (strcmp(state, "Top") == 0)    return 0;
  if (strcmp(state, "Bottom") == 0) return 1;
  if (strcmp(state, "Middle") == 0) return 2;
  if (strcmp(state, "End") == 0)    return 3;
  return 4;
}

void copyLinescoreSnapshot(int gamePk, JsonObjectConst src, LinescoreSnapshot& dst) {
  memset(&dst, 0, sizeof(dst));
  dst.gamePk         = gamePk;
  dst.awayScore      = src["teams"]["away"]["runs"]      | 0;
  dst.homeScore      = src["teams"]["home"]["runs"]      | 0;
  dst.balls          = src["balls"]                      | 0;
  dst.strikes        = src["strikes"]                    | 0;
  dst.outs           = src["outs"]                       | 0;
  dst.currentInning  = src["currentInning"]              | 0;
  dst.inningState    = classifyInningState(src["inningState"] | "");
  dst.valid = true;
}

// Keeps only the LIVE games from today's slate — that list is the whole
// basis for the preferred-team game selector on the render core.
void copyOtherGames(JsonObjectConst scheduleRoot, ScheduleSnapshot& dst) {
  memset(&dst, 0, sizeof(dst));
  JsonArrayConst dates = scheduleRoot["dates"].as<JsonArrayConst>();
  if (dates.isNull() || dates.size() == 0) return;
  JsonArrayConst games = dates[0]["games"].as<JsonArrayConst>();
  if (games.isNull()) return;

  for (JsonObjectConst game : games) {
    if (dst.otherCount >= 9) break;
    const char* state = game["status"]["abstractGameState"] | "";
    if (strcmp(state, "Live") != 0) continue;
    OtherGameLite& slot = dst.others[dst.otherCount++];
    slot.gamePk     = game["gamePk"] | 0;
    slot.awayTeamId = game["teams"]["away"]["team"]["id"] | 0;
    slot.homeTeamId = game["teams"]["home"]["team"]["id"] | 0;
  }
  dst.valid = true;
}

bool fetchLinescore(int gamePk, LinescoreSnapshot& out) {
  if (!fetchMlbLinescore(gamePk, mlbDoc)) return false;
  copyLinescoreSnapshot(gamePk, mlbDoc.as<JsonObjectConst>(), out);
  mlbDoc.clear();
  return out.valid;
}

bool fetchScheduleSnapshot(ScheduleSnapshot& out) {
  // Bump the attempt counter immediately so the render core can see the
  // data task is alive even when every fetch fails.
  bumpScheduleFetchAttempt();
  // Today's slate (server-side "today", so no clock is needed). A day with
  // no games parses fine and leaves the list empty — that's a valid,
  // successful fetch, not a failure.
  if (!fetchMlbSchedule(nullptr, mlbDoc)) {
    setScheduleLastError("today_fetch_failed");
    return false;
  }
  copyOtherGames(mlbDoc.as<JsonObjectConst>(), out);
  mlbDoc.clear();

  bumpScheduleFetchSuccess();
  setScheduleLastError("ok");
  DBG_PRINTF("[SCHED] fetched OK on attempt %u\n",
             (unsigned)getScheduleFetchSuccesses());
  return out.valid;
}

void mlbDataTaskLoop(void*) {
  // Schedule goes first in each tick: it is the primary display feed, and at
  // boot we want it to win the race for the first (often only) healthy
  // connection window.
  static uint32_t lastScheduleAt = 0;
  static bool lastScheduleOk = true;
  static uint32_t scheduleRetryInterval = MLB_SCHEDULE_RETRY_MS;

  for (;;) {
    // Yield to the network task. We only do work when online.
    static uint32_t sOnlineSince = 0;
    if (isOnline()) {
      if (sOnlineSince == 0) sOnlineSince = millis();
    } else {
      sOnlineSince = 0;
    }
    if (sOnlineSince != 0) {
      uint32_t onlineFor = millis() - sOnlineSince;
      // Firmware self-update check (manifest + OTA download). While a
      // download is running, hold off the feed fetches entirely: the TLS
      // download needs the heap and airtime to itself.
      if (otaCheckRequested()) {
        // Free the JSON pool and response buffer so a portal-triggered
        // mid-session TLS check gets the largest contiguous heap we can
        // offer (see the note at the top of ota_update.cpp).
        mlbDoc.clear();
        mlbDoc.shrinkToFit();
        http_fetch::releaseBodyBuffer();
      }
      serviceOtaUpdates(onlineFor);
      if (otaUpdateInProgress()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      // Portal priority mode: someone is actively using the setup pages —
      // pause all feed activity (and drop the statsapi session) so the web
      // server gets the core and the radio airtime to itself. Cached data
      // keeps the display going; feeds resume when the portal goes idle.
      if (portalEngaged()) {
        http_fetch::closeSession();
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }

      // The first OTA check runs before any feed fetch: its TLS handshake
      // needs the pristine boot heap (see otaBootGateReached).
      if (!otaBootGateReached(onlineFor)) {
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      uint32_t now = millis();

      // Schedule: refresh ~once a minute after a successful fetch. While the
      // last fetch failed, retry after scheduleRetryInterval, which doubles
      // per consecutive failure (capped) — hammering a network that is
      // refusing connections only prolongs the lockout.
      bool firstEver = (lastScheduleAt == 0);
      bool due       = (now - lastScheduleAt >= MLB_SCHEDULE_POLL_INTERVAL_MS);
      bool retryDue  = (!lastScheduleOk &&
                        now - lastScheduleAt >= scheduleRetryInterval);
      if (firstEver || due || retryDue) {
        lastScheduleAt = now;
        ScheduleSnapshot sch;
        if (fetchScheduleSnapshot(sch)) {
          mlb_data::publishSchedule(sch);
        }
        // Judge success at the fetch level (lastError == "ok"), not by
        // out.valid — a day with no games parses fine but leaves no live
        // games to publish, and that shouldn't trigger fast retries.
        lastScheduleOk = (strcmp(getScheduleLastError(), "ok") == 0);
        if (lastScheduleOk) {
          scheduleRetryInterval = MLB_SCHEDULE_RETRY_MS;
        } else {
          scheduleRetryInterval =
              (scheduleRetryInterval * 2 > MLB_RETRY_BACKOFF_MAX_MS)
                  ? MLB_RETRY_BACKOFF_MAX_MS
                  : scheduleRetryInterval * 2;
        }
      }

      int activeGamePk = getActiveGamePk();
      static uint32_t lastLivePollAt = 0;
      if (activeGamePk > 0 && now - lastLivePollAt >= MLB_LIVE_POLL_INTERVAL_MS) {
        lastLivePollAt = now;
        LinescoreSnapshot ls;
        if (fetchLinescore(activeGamePk, ls)) {
          mlb_data::publishLinescore(ls);
        }
      }

      // NOTE: the statsapi keep-alive session is deliberately left OPEN
      // between ticks. The install's network path refuses new TCP flows
      // from this device after a burst of connections, so reusing one
      // session across refreshes (and across the 5 s live polls during a
      // game) is what keeps the feeds alive. The OTA updater closes it
      // before its own connection; the next statsapi fetch reopens it.
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

} // namespace

void startMlbDataTask() {
  xTaskCreatePinnedToCore(mlbDataTaskLoop, "MlbData", 12288,
                          nullptr, 1, nullptr, 0);
}
