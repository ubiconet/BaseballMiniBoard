#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "common/ui/display_test.h"
#include "common/ui/ota_indicator.h"
#include "ota_update.h"

#include "network_service.h"

namespace {
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// PROVISIONING: no network, portal + setup screen locked.
// CONNECTING:    associating / probing for internet (portal stays available).
// ONLINE:        internet confirmed; AP torn down, setup screen releases.
enum NetworkState { PROVISIONING, CONNECTING, ONLINE };
NetworkState state = PROVISIONING;

const char DEFAULT_CONFIG[] = R"json({"version":1,"teams":[]})json";

bool dnsRunning = false;
bool apUp = false;
bool otaStarted = false;
// Branding + team options, injected at startNetworkServices() by the app
// shell (from the sport's config/tables). Empty until then.
NetworkBranding netBranding = {"Scoreboard", "SCOREBOARD", "scoreboard"};
const NetworkTeamOption* netTeamOptions = nullptr;
size_t netTeamOptionCount = 0;
int netDefaultTeams[3] = {0, 0, 0};
String deviceHostname;
// Per-device setup AP SSID (set once MAC is known) so several
// unprovisioned boards can be powered at once without SSID collisions.
String apSsid;

// Credentials in NVS (last known good) and the pair being tried from the portal.
String savedSsid;
String savedPassword;
String pendingSsid;
String pendingPassword;
bool hasPending = false;
bool clockDisplayEnabled = true;

// Display timezone, persisted in NVS ("tz") and selected in the portal.
// POSIX TZ strings (not IANA names): they are self-contained — DST rules and
// all — so no tz database needs to ship in the firmware. FACTORY_DEFAULT_TIMEZONE
// (src/config.h) only covers the very first boot before a selection is saved.
String tzString;

struct TzOption {
  const char* label;
  const char* posix;
};
const TzOption TZ_OPTIONS[] = {
  {"UTC",                                    "UTC0"},
  {"US Eastern (New York)",                  "EST5EDT,M3.2.0,M11.1.0"},
  {"US Central (Chicago)",                   "CST6CDT,M3.2.0,M11.1.0"},
  {"US Mountain (Denver)",                   "MST7MDT,M3.2.0,M11.1.0"},
  {"US Arizona (no DST)",                    "MST7"},
  {"US Pacific (Los Angeles)",               "PST8PDT,M3.2.0,M11.1.0"},
  {"Alaska (Anchorage)",                     "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Hawaii (Honolulu)",                      "HST10"},
  {"Canada Atlantic (Halifax)",              "AST4ADT,M3.2.0,M11.1.0"},
  {"Canada Newfoundland",                    "NST3:30NDT,M3.2.0,M11.1.0"},
  {"UK & Ireland",                           "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Central Europe (Paris/Berlin)",          "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Eastern Europe (Athens/Helsinki)",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"India",                                  "IST-5:30"},
  {"Japan (Tokyo)",                          "JST-9"},
  {"Australia Eastern (Sydney)",             "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Australia Western (Perth)",              "AWST-8"},
};
const size_t TZ_OPTIONS_COUNT = sizeof(TZ_OPTIONS) / sizeof(TZ_OPTIONS[0]);

// Only strings from TZ_OPTIONS are accepted from the portal — never feed the
// TZ environment variable arbitrary user input.
bool isValidTzString(const char* posix) {
  if (posix == nullptr) return false;
  for (size_t i = 0; i < TZ_OPTIONS_COUNT; ++i) {
    if (strcmp(TZ_OPTIONS[i].posix, posix) == 0) return true;
  }
  return false;
}

// Applies the stored timezone to the C library (localtime/mktime everywhere —
// idle matrix clock, upcoming-game times, the schedule-day window — follows
// it). Time itself was already NTP-synced by the shell's one-shot
// configTzTime(); a mid-session change only needs the TZ update, not a
// re-sync. Runs on the network task; a concurrent localtime on the render
// core can at worst produce one odd frame during the switch.
void applyTimezone() {
  setenv("TZ", tzString.c_str(), 1);
  tzset();
  DBG_PRINTF("[NET] timezone applied: %s\n", tzString.c_str());
}

String buildTzOptionsHtml(const char* selected) {
  String html = "";
  for (size_t i = 0; i < TZ_OPTIONS_COUNT; ++i) {
    html += "<option value=\"";
    html += TZ_OPTIONS[i].posix;
    html += "\"";
    if (strcmp(TZ_OPTIONS[i].posix, selected) == 0) html += " selected";
    html += ">";
    html += TZ_OPTIONS[i].label;
    html += "</option>";
  }
  return html;
}

uint32_t stateStartedAt = 0;
uint32_t lastProbeAt = 0;
uint32_t disconnectStartedAt = 0;
uint32_t onlineAt = 0;
uint32_t lastReconnectAt = 0;
uint32_t lastDebugAt = 0;
wl_status_t lastLoggedWiFiStatus = WL_NO_SHIELD;
bool setupScreenVisible = false;
// Portal priority mode: while someone is actively using the setup pages,
// background work pauses so the web server gets the core and the radio to
// itself (page loads over this device's marginal Wi-Fi stall otherwise).
// Every request refreshes the window.
uint32_t portalActiveUntil = 0;
void markPortalActivity() {
  portalActiveUntil = millis() + PORTAL_ACTIVITY_WINDOW_MS;
}

// Only delays the very first boot's CONNECTING->ONLINE transition (see runNetworkStateMachine),
// so the Wi-Fi setup/connecting screen isn't just a flash; later reconnects skip this.
uint32_t networkTaskStartedAt = 0;
bool firstBootConnectHeld = true;

// Cross-task handoff to the main loop, which owns the display. The network
// task only sets these; handleNetworkDisplay() reads and clears them.
enum SetupDisplayMode { SETUP_CONNECTING, SETUP_AP_INSTRUCTIONS, SETUP_ONLINE_PORTAL };
volatile bool redrawSetupPending = false;
volatile SetupDisplayMode redrawModeV = SETUP_CONNECTING;
volatile uint32_t setupIpV = 0;
volatile bool releasePending = false;

// Async scan cache, only touched from the network task (server handlers run there).
String scanOptionsHtml;
bool scanActive = false;
uint32_t lastScanAt = 0;

void requestSetupRedraw(SetupDisplayMode mode, IPAddress ip) {
  redrawModeV = mode;
  setupIpV = ip;
  redrawSetupPending = true;
}

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED: return "CONNECTED";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    case WL_IDLE_STATUS: return "IDLE";
    default: return "OTHER";
  }
}

void logWiFiStatus(bool force = false) {
  wl_status_t status = WiFi.status();
  if (!force && status == lastLoggedWiFiStatus && millis() - lastDebugAt < NETWORK_DEBUG_INTERVAL_MS) {
    return;
  }
  lastLoggedWiFiStatus = status;
  lastDebugAt = millis();
  Serial.printf("[NET %lu] state=%s wifi=%s rssi=%d ip=%s ap=%s\n",
                millis(),
                state == ONLINE ? "ONLINE" : (state == CONNECTING ? "CONNECTING" : "PROVISIONING"),
                wifiStatusName(status),
                WiFi.RSSI(),
                WiFi.localIP().toString().c_str(),
                WiFi.softAPIP().toString().c_str());
}

void startPortalInfrastructure() {
  if (!dnsRunning) {
    dnsServer.start(53, "*", WiFi.softAPIP());
    dnsRunning = true;
  }
}

bool ensureSetupAccessPoint() {
  if (!apUp) {
    if (!WiFi.softAP(apSsid.c_str(), NETWORK_AP_PASSWORD)) {
      Serial.println("[NET] ERROR: failed to start setup access point");
      return false;
    }
    apUp = true;
    Serial.printf("[NET] AP ready: ssid=%s ip=%s\n",
                  apSsid.c_str(),
                  WiFi.softAPIP().toString().c_str());
  }
  startPortalInfrastructure();
  return true;
}

void stopPortalInfrastructure() {
  if (dnsRunning) {
    dnsServer.stop();
    dnsRunning = false;
  }
  if (apUp) {
    WiFi.softAPdisconnect(true);
    apUp = false;
  }
}

bool probeInternet() {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin(NETWORK_PROBE_ANCHOR_URL);
  int code = http.GET();
  http.end();
  return code > 0;
}

void startArduinoOTA() {
  if (otaStarted) {
    return;
  }
  ArduinoOTA.setHostname(deviceHostname.c_str());
  ArduinoOTA.setPassword(NETWORK_OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Network update starting");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] Network update complete");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]\n", error);
  });
  ArduinoOTA.begin();
  otaStarted = true;
  Serial.println("[OTA] Arduino network OTA ready");
}

void enterOnline() {
  state = ONLINE;
  onlineAt = millis();
  if (hasPending) {
    preferences.begin("network", false);
    preferences.putString("ssid", pendingSsid);
    preferences.putString("password", pendingPassword);
    preferences.end();
    savedSsid = pendingSsid;
    savedPassword = pendingPassword;
    hasPending = false;
  }
  stopPortalInfrastructure();
  startArduinoOTA();
  requestSetupRedraw(SETUP_ONLINE_PORTAL, WiFi.localIP());
  Serial.printf("[NET] Online: ip=%s rssi=%d gateway=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI(),
                WiFi.gatewayIP().toString().c_str());
  logWiFiStatus(true);
}

void enterProvisioning() {
  state = PROVISIONING;
  stateStartedAt = millis();
  WiFi.disconnect();
  if (ensureSetupAccessPoint()) {
    requestSetupRedraw(SETUP_AP_INSTRUCTIONS, WiFi.softAPIP());
  }
  Serial.println("[NET] Provisioning: waiting for portal credentials");
  logWiFiStatus(true);
}

void enterConnecting() {
  state = CONNECTING;
  stateStartedAt = millis();
  lastProbeAt = 0;
  lastReconnectAt = millis();
  ensureSetupAccessPoint();
  Serial.printf("[NET] Connecting to SSID '%s'\n", savedSsid.c_str());
  requestSetupRedraw(SETUP_CONNECTING, WiFi.softAPIP());
}

void tryReconnectWithSavedNetwork() {
  if (savedSsid.isEmpty()) {
    enterProvisioning();
    return;
  }
  // Keep the modem awake. Default Wi-Fi power save (min-modem sleep) makes
  // the ESP32 miss beacons on a marginal link and disassociate in storms a
  // minute or so after connect — exactly the drop/reconnect cycles that
  // killed the schedule/news fetches. The scoreboard is mains-powered, so
  // the extra ~40 mA is irrelevant.
  WiFi.setSleep(false);
  ensureSetupAccessPoint();
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
  enterConnecting();
}

int prefTeam1 = 0; // populated from netDefaultTeams in loadSavedNetwork()
int prefTeam2 = 0;
int prefTeam3 = 0;

void loadSavedNetwork() {
  preferences.begin("network", true);
  savedSsid = preferences.getString("ssid", "");
  savedPassword = preferences.getString("password", "");
  prefTeam1 = preferences.getInt("team1", netDefaultTeams[0]);
  prefTeam2 = preferences.getInt("team2", netDefaultTeams[1]);
  prefTeam3 = preferences.getInt("team3", netDefaultTeams[2]);
  clockDisplayEnabled = preferences.getBool("show_clock", true);
  tzString = preferences.getString("tz", FACTORY_DEFAULT_TIMEZONE);
  if (!isValidTzString(tzString.c_str())) {
    tzString = FACTORY_DEFAULT_TIMEZONE;  // unknown/legacy value: fall back
  }
  applyTimezone();
  preferences.end();
}

String buildTeamOptionsHtml(int selectedId) {
  String html = "";
  for (size_t i = 0; i < netTeamOptionCount; i++) {
    html += "<option value=\"";
    html += String(netTeamOptions[i].id);
    html += "\"";
    if (netTeamOptions[i].id == selectedId) html += " selected";
    html += ">";
    html += netTeamOptions[i].label;
    html += "</option>";
  }
  return html;
}


String htmlEscape(const String& text) {
  String out;
  out.reserve(text.length());
  for (size_t i = 0; i < text.length(); ++i) {
    char c = text[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

void refreshScanCache() {
  int8_t result = WiFi.scanComplete();
  if (scanActive && result >= 0) {
    scanOptionsHtml = "";
    for (int index = 0; index < result; ++index) {
      String ssid = WiFi.SSID(index);
      if (!ssid.isEmpty()) {
        String escaped = htmlEscape(ssid);
        scanOptionsHtml += "<option value=\"" + escaped + "\">" + escaped + " (" +
                           String(WiFi.RSSI(index)) + " dBm)</option>";
      }
    }
    WiFi.scanDelete();
    scanActive = false;
    lastScanAt = millis();
  } else if (result == WIFI_SCAN_FAILED) {
    scanActive = false;
    lastScanAt = millis();
  }
  if (!scanActive && state != ONLINE && millis() - lastScanAt > NETWORK_SCAN_REFRESH_MS) {
    WiFi.scanNetworks(true, true);
    scanActive = true;
  }
}

// This build has no TFT: the setup instructions that used to fill the
// provisioning screen go to Serial instead. The AP itself is discoverable
// from any phone (broadcast SSID), and the captive portal redirects to the
// setup page, so onboarding works headless.
void printSetupInstructions(const String& portalAddress, bool stationConnected) {
  String portalUrl = "http://" + portalAddress + "/";
  if (stationConnected) {
    Serial.println("[NET] Wi-Fi connected. Setup portal:");
    Serial.print("  ");
    Serial.println(portalUrl);
    return;
  }
  Serial.println("[NET] Setup required. Connect to this Wi-Fi network:");
  Serial.print("  SSID: ");
  Serial.println(apSsid);
  Serial.print("  Password: ");
  Serial.println(NETWORK_AP_PASSWORD);
  Serial.println("Then open the setup portal (captive redirect, or):");
  Serial.print("  ");
  Serial.println(portalUrl);
}

String readConfig() {
  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    return DEFAULT_CONFIG;
  }
  String config = file.readString();
  file.close();
  return config;
}

void redirectToPortal() {
  markPortalActivity();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void servePortal() {
  markPortalActivity();
  server.sendHeader("Cache-Control", "max-age=300");
  String page = R"html(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>@@NAME@@ Setup</title><style>
body{margin:0;background:#061b46;color:#fff;font:16px system-ui,sans-serif}
main{max-width:440px;margin:5vh auto;padding:24px;background:#0b2b62;border:2px solid #dfe9ff;border-radius:8px}
h1{margin-top:0;font-size:24px}label{display:block;margin:14px 0 4px;font-weight:600}input,select{box-sizing:border-box;width:100%;padding:10px;border:0;border-radius:4px;font-size:15px}
button{margin-top:20px;width:100%;padding:12px;background:#f5c400;border:0;border-radius:4px;font-weight:700;font-size:16px;color:#000;cursor:pointer}.hint{color:#c5d3ee;font-size:14px;line-height:1.4}
hr{border:0;border-top:1px solid #1c4587;margin:20px 0}
</style></head><body><main><h1>@@NAME@@ Setup</h1>
<p class="hint">Configure Wi-Fi connection and select your favorite teams in order of priority.</p>
<form method="post" action="/save">
<label for="network">Nearby Wi-Fi Networks</label>
<select id="network" onchange="ssid.value=this.value"><option value="">Enter network manually</option>)html";
  page += scanOptionsHtml;
  page += R"html(</select>
<label for="ssid">Wi-Fi Network Name</label><input id="ssid" name="ssid" value=")html";
  page += htmlEscape(savedSsid);
  page += R"html(" required maxlength="32" autocomplete="off">
<label for="password">Wi-Fi Password</label><input id="password" name="password" type="password" maxlength="63" autocomplete="off">
<p class="hint">Leave the password blank and the network unchanged to save team preferences without touching the connection.</p>
<hr>
<h3>Favorite Team Priorities</h3>
<label for="team1">Priority 1 Team (Primary)</label><select id="team1" name="team1">)html";
  page += buildTeamOptionsHtml(prefTeam1);
  page += R"html(</select>
<label for="team2">Priority 2 Team</label><select id="team2" name="team2">)html";
  page += buildTeamOptionsHtml(prefTeam2);
  page += R"html(</select>
<label for="team3">Priority 3 Team</label><select id="team3" name="team3">)html";
  page += buildTeamOptionsHtml(prefTeam3);
  page += R"html(</select>
<hr><h3>Display Time Zone</h3>
<label for="tz">Used for game times, countdowns, and the idle clock</label>
<select id="tz" name="tz">)html";
  page += buildTzOptionsHtml(tzString.c_str());
  page += R"html(</select>
<hr><label style="display:flex;align-items:center;gap:10px" for="show-clock"><input style="width:auto" id="show-clock" name="show_clock" type="checkbox" value="1")html";
  if (clockDisplayEnabled) page += " checked";
  page += R"html(>Display current time on score boards when no game is live</label>
<button type="submit">Save & Connect Scoreboard</button></form>
<hr><h3>Display Test</h3>
<p class="hint">Test each count LED and every pixel on both score matrices.</p>
<p><a style="color:#f5c400" href="/display-test">Open display test page</a></p>
<hr><h3>Firmware Update</h3>
<p class="hint">Installed: )html" + String(FIRMWARE_VERSION) + R"html(. Automatic checks run at boot and periodically.</p>
<button type="button" style="margin-top:8px" onclick="otaCheck()">Check for Update Now</button>
<p class="hint" id="otaStatus">&nbsp;</p>
<p class="hint">Latest binary (for manual updates):<br>
<a style="color:#f5c400;word-break:break-all" href=")html" + String(OTA_LATEST_BIN_URL) + R"html(">)html" + String(OTA_LATEST_BIN_URL) + R"html(</a></p>
<p><a style="color:#f5c400" href="/update">Upload a firmware file manually</a></p>
<script>
var otaWaiting=false;
function otaCheck(){otaWaiting=true;document.getElementById('otaStatus').textContent='Checking...';fetch('/ota/check',{method:'POST'})}
setInterval(function(){fetch('/ota/status').then(function(r){return r.json()}).then(function(s){var e=document.getElementById('otaStatus');
if(s.stage==='DOWNLOADING'){otaWaiting=false;e.textContent='Downloading update '+s.progress+'% - watch the Home matrix and count LEDs; do not power off.'}
else if(s.stage==='REBOOTING'){otaWaiting=false;e.textContent='Update installed - rebooting...'}
else if(s.stage==='FAILED'){otaWaiting=false;e.textContent='Update failed (network may block GitHub) - use the manual upload below.'}
else if(otaWaiting&&s.checked&&!s.ok){otaWaiting=false;e.textContent='Check failed - this network may block GitHub. Use the manual upload below.'}
else if(otaWaiting&&s.checked&&s.ok){otaWaiting=false;e.textContent='No update available - firmware is current.'}
})},2000);
</script>
<hr><p class="hint"><a style="color:#f5c400" href="/update">Upload new firmware (.bin)</a></p></main></body></html>)html";
  page.replace("@@NAME@@", netBranding.deviceName);
  server.send(200, "text/html", page);
}

void serveStatus() {
  markPortalActivity();
  const char* name = state == ONLINE ? "online" : (state == CONNECTING ? "connecting" : "provisioning");
  server.send(200, "application/json", String("{\"state\":\"") + name + "\"}");
}

void saveNetwork() {
  markPortalActivity();
  pendingSsid = server.arg("ssid");
  pendingPassword = server.arg("password");
  if (server.hasArg("team1")) prefTeam1 = server.arg("team1").toInt();
  if (server.hasArg("team2")) prefTeam2 = server.arg("team2").toInt();
  if (server.hasArg("team3")) prefTeam3 = server.arg("team3").toInt();
  clockDisplayEnabled = server.hasArg("show_clock");
  // Timezone is validated against the option table before it is trusted.
  bool tzChanged = false;
  if (server.hasArg("tz")) {
    String requestedTz = server.arg("tz");
    if (isValidTzString(requestedTz.c_str()) &&
        !requestedTz.equals(tzString)) {
      tzString = requestedTz;
      tzChanged = true;
    }
  }

  // Network settings only count as changed when a new SSID is supplied or a
  // password is (re-)entered; otherwise this is a team-preferences-only save
  // and the Wi-Fi connection must be left alone.
  bool networkChanging = !pendingSsid.isEmpty() &&
                         (pendingSsid != savedSsid || !pendingPassword.isEmpty());

  if (savedSsid.isEmpty() && !networkChanging) {
    server.send(400, "text/plain", "Wi-Fi network name is required");
    return;
  }

  // Team/timezone preferences always persist; credentials only when they changed.
  preferences.begin("network", false);
  preferences.putInt("team1", prefTeam1);
  preferences.putInt("team2", prefTeam2);
  preferences.putInt("team3", prefTeam3);
  preferences.putBool("show_clock", clockDisplayEnabled);
  preferences.putString("tz", tzString);
  if (networkChanging) {
    preferences.putString("ssid", pendingSsid);
    preferences.putString("password", pendingPassword);
  }
  preferences.end();

  if (tzChanged) {
    applyTimezone();  // game times + idle clock follow on the next render
  }

  if (!networkChanging) {
    Serial.printf("[NET] Preferences-only save: teams=[%d, %d, %d] tz=%s (network untouched)\n",
                  prefTeam1, prefTeam2, prefTeam3, tzString.c_str());
    server.send(200, "text/html", R"html(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title><style>body{margin:0;background:#061b46;color:#fff;font:16px system-ui,sans-serif}
main{max-width:420px;margin:8vh auto;padding:28px;background:#0b2b62;border:2px solid #dfe9ff;border-radius:8px;text-align:center}
</style></head><body><main><h1>Settings Saved</h1>
<p>Team priorities and time zone updated — the scoreboard picks them up within a minute.</p>
<p>Wi-Fi settings were not changed.</p>
<p><a style="color:#f5c400" href="/">Back to configuration</a></p></main></body></html>)html");
    return;
  }

  savedSsid = pendingSsid;
  savedPassword = pendingPassword;
  hasPending = true;

  WiFi.disconnect();
  WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
  enterConnecting();

  server.send(200, "text/html", R"html(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Connecting</title><style>body{margin:0;background:#061b46;color:#fff;font:16px system-ui,sans-serif}
main{max-width:420px;margin:8vh auto;padding:28px;background:#0b2b62;border:2px solid #dfe9ff;border-radius:8px;text-align:center}
#status{font-size:20px;font-weight:700}</style></head><body><main><h1>Connecting...</h1>
<p id="status">Trying the network and checking internet access.</p>
<p><a style="color:#f5c400" href="/">Back to configuration</a></p>
<script>setInterval(function(){fetch('/status').then(function(r){return r.json()}).then(function(s){
if(s.state==='online'){document.getElementById('status').textContent='Connected! The scoreboard is going online.'}
else if(s.state==='provisioning'){document.getElementById('status').textContent='Could not connect or no internet. Check the password and try again.'}
})},2000)</script></main></body></html>)html");
  Serial.printf("Saved network '%s' and Teams [%d, %d, %d]\n", pendingSsid.c_str(), prefTeam1, prefTeam2, prefTeam3);
}


void saveConfig() {
  markPortalActivity();
  if (server.arg("portal") != NETWORK_PORTAL_PASSWORD) {
    server.send(401, "text/plain", "Invalid portal password");
    return;
  }
  JsonDocument document;
  if (deserializeJson(document, server.arg("config"))) {
    server.send(400, "text/plain", "Invalid JSON configuration");
    return;
  }
  File file = LittleFS.open("/config.tmp", "w");
  if (!file) {
    server.send(500, "text/plain", "Unable to write configuration");
    return;
  }
  serializeJson(document, file);
  file.close();
  LittleFS.remove("/config.json");
  LittleFS.rename("/config.tmp", "/config.json");
  server.send(200, "text/html", "<h1>Saved</h1><p>Runtime settings updated.</p>");
}

void serveConfig() {
  markPortalActivity();
  server.send(200, "application/json", readConfig());
}

void handleOtaCheckNow() {
  markPortalActivity();
  if (!isOnline()) {
    server.send(503, "text/plain", "Not online");
    return;
  }
  requestOtaCheckNow();
  server.send(200, "application/json", "{\"ok\":true}");
}

void serveOtaStatus() {
  markPortalActivity();
  const char* stage = "NONE";
  switch (getOtaStage()) {
    case OtaStage::DOWNLOADING: stage = "DOWNLOADING"; break;
    case OtaStage::REBOOTING:   stage = "REBOOTING";   break;
    case OtaStage::FAILED:      stage = "FAILED";      break;
    default: break;
  }
  char body[96];
  snprintf(body, sizeof(body),
           "{\"stage\":\"%s\",\"progress\":%d,\"checked\":%s,\"ok\":%s}",
           stage, getOtaProgress(),
           otaEverChecked() ? "true" : "false",
           otaLastCheckOk() ? "true" : "false");
  server.send(200, "application/json", body);
}

void sendDisplayTestState() {
  DisplayTestState test = getRequestedDisplayTestState();
  char body[112];
  snprintf(body, sizeof(body),
           "{\"active\":%s,\"leds\":%u,\"away\":\"%016llx\","
           "\"home\":\"%016llx\"}",
           test.active ? "true" : "false", test.ledMask,
           (unsigned long long)test.awayPixels,
           (unsigned long long)test.homePixels);
  server.send(200, "application/json", body);
}

bool parseDisplayTestBoolean(const char* name, bool& value) {
  if (!server.hasArg(name)) return false;
  String argument = server.arg(name);
  if (argument == "1") {
    value = true;
    return true;
  }
  if (argument == "0") {
    value = false;
    return true;
  }
  return false;
}

bool parseDisplayTestCoordinate(const char* name, uint8_t& value) {
  if (!server.hasArg(name)) return false;
  String argument = server.arg(name);
  if (argument.isEmpty()) return false;
  char* end = nullptr;
  long parsed = strtol(argument.c_str(), &end, 10);
  if (end == argument.c_str() || *end != '\0' || parsed < 0 || parsed > 7) {
    return false;
  }
  value = static_cast<uint8_t>(parsed);
  return true;
}

void serveDisplayTestState() {
  markPortalActivity();
  sendDisplayTestState();
}

void serveDisplayTestPage() {
  markPortalActivity();
  server.send(200, "text/html", R"html(<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Display Test</title><style>
body{margin:0;background:#061b46;color:#fff;font:16px system-ui,sans-serif}
main{max-width:720px;margin:4vh auto;padding:22px;background:#0b2b62;border:2px solid #dfe9ff;border-radius:8px}
h1{margin-top:0}h2{font-size:19px}.hint{color:#c5d3ee;line-height:1.4}
a{color:#f5c400}.leds{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
button{border:1px solid #8299c2;border-radius:5px;padding:10px;background:#102d5a;color:#fff;font:inherit;cursor:pointer}
button.on{background:#f5c400;color:#000;border-color:#f5c400}
.matrices{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:18px}
.matrix{display:grid;grid-template-columns:repeat(8,1fr);gap:3px;max-width:280px}
.pixel{aspect-ratio:1;padding:0;min-width:18px;min-height:18px;background:#061b46}
.pixel.on{background:#f5c400}
.stop{margin:20px 0;background:#9b2633;border-color:#d66}
</style></head><body><main><h1>Display Test</h1>
<p class="hint">Tap a control to toggle one count LED or one matrix pixel. The test takes over the board until you stop it.</p>
<h2>Count LEDs</h2><div class="leds" id="leds"></div>
<h2>8×8 Matrices</h2><div class="matrices">
<section><h3>Away matrix (position 1)</h3><div class="matrix" id="away"></div></section>
<section><h3>Home / inning matrix (position 2)</h3><div class="matrix" id="home"></div></section>
</div><p id="message" class="hint"></p>
<button class="stop" onclick="stopTest()">Stop test and restore scoreboard</button>
<p><a href="/">Back to board settings</a></p></main>
<script>
var state={active:false,leds:0,away:'0000000000000000',home:'0000000000000000'};
var ledNames=['Ball 1','Ball 2','Ball 3','Strike 1','Strike 2','Out 1','Out 2'];
function pixelOn(hex,index){var shift=index%4;var digit=15-Math.floor(index/4);return ((parseInt(hex.charAt(digit),16)>>shift)&1)!==0}
function updateHex(hex,index,on){var digits=hex.split(''),digit=15-Math.floor(index/4),bit=1<<(index%4),value=parseInt(digits[digit],16);digits[digit]=(on?(value|bit):(value&~bit)).toString(16);return digits.join('')}
function post(path,data){return fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)}).then(function(r){if(!r.ok)throw new Error('Request failed');return r.json()}).then(function(s){state=s;draw()}).catch(function(e){document.getElementById('message').textContent=e.message})}
function draw(){var leds=document.getElementById('leds');leds.textContent='';
ledNames.forEach(function(name,index){var b=document.createElement('button'),on=(state.leds&(1<<index))!==0;b.textContent=name+(on?' — ON':' — OFF');b.className=on?'on':'';b.onclick=function(){post('/display-test/led',{index:index,on:on?'0':'1'})};leds.appendChild(b)});
['away','home'].forEach(function(which){var grid=document.getElementById(which);grid.textContent='';for(var y=0;y<8;y++)for(var x=0;x<8;x++){var index=y*8+x,on=pixelOn(state[which],index),b=document.createElement('button');b.className='pixel'+(on?' on':'');b.setAttribute('aria-label',which+' pixel '+(x+1)+','+(y+1));b.onclick=(function(matrix,px,py,enabled){return function(){var key=matrix;state[key]=updateHex(state[key],py*8+px,enabled);post('/display-test/pixel',{matrix:matrix,x:px,y:py,on:enabled?'0':'1'})}})(which,x,y,on);grid.appendChild(b)}});
document.getElementById('message').textContent=state.active?'Display test active.':'';
}
function stopTest(){fetch('/display-test/stop',{method:'POST'}).then(function(){location.href='/'})}
fetch('/display-test/state').then(function(r){return r.json()}).then(function(s){state=s;draw()}).catch(function(){document.getElementById('message').textContent='Unable to read display state.'});
</script></body></html>)html");
}

void handleDisplayTestLed() {
  markPortalActivity();
  uint8_t index = 0;
  bool enabled = false;
  if (!parseDisplayTestCoordinate("index", index) ||
      !parseDisplayTestBoolean("on", enabled) ||
      !setDisplayTestLed(index, enabled)) {
    server.send(400, "text/plain", "Expected LED index 0..6 and on=0|1");
    return;
  }
  sendDisplayTestState();
}

void handleDisplayTestPixel() {
  markPortalActivity();
  uint8_t x = 0;
  uint8_t y = 0;
  bool enabled = false;
  String matrix = server.arg("matrix");
  if (!parseDisplayTestCoordinate("x", x) ||
      !parseDisplayTestCoordinate("y", y) ||
      !parseDisplayTestBoolean("on", enabled) ||
      (matrix != "away" && matrix != "home")) {
    server.send(400, "text/plain",
                "Expected matrix=away|home, x/y=0..7 and on=0|1");
    return;
  }
  setDisplayTestPixel(matrix == "home", x, y, enabled);
  sendDisplayTestState();
}

void handleDisplayTestStop() {
  markPortalActivity();
  stopDisplayTest();
  sendDisplayTestState();
}

void serveUpdatePage() {
  markPortalActivity();
  server.send(200, "text/html", R"html(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware Update</title><style>
body{margin:0;background:#061b46;color:#fff;font:16px system-ui,sans-serif}
main{max-width:440px;margin:8vh auto;padding:24px;background:#0b2b62;border:2px solid #dfe9ff;border-radius:8px}
h1{margin-top:0;font-size:24px}input{box-sizing:border-box;width:100%;padding:10px;border:0;border-radius:4px;font-size:15px;background:#fff}
button{margin-top:16px;width:100%;padding:12px;background:#f5c400;border:0;border-radius:4px;font-weight:700;font-size:16px;color:#000;cursor:pointer}
.hint{color:#c5d3ee;font-size:14px;line-height:1.4}
</style></head><body><main><h1>Firmware Update</h1>
<p class="hint">Upload a new compiled .bin firmware image. The scoreboard will reboot automatically once the update finishes.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin" required>
<button type="submit">Upload & Flash</button>
</form></main></body></html>)html");
}

void handleUpdateUpload() {
  markPortalActivity();
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[UPDATE] Receiving firmware: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[UPDATE] Success: %u bytes. Rebooting...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
  }
}

void handleUpdateResult() {
  markPortalActivity();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", Update.hasError() ? "Update FAILED" : "Update OK, rebooting...");
  delay(500);
  ESP.restart();
}

void registerPortalRoutes() {
  server.on("/", HTTP_GET, servePortal);
  server.on("/display-test", HTTP_GET, serveDisplayTestPage);
  server.on("/display-test/state", HTTP_GET, serveDisplayTestState);
  server.on("/display-test/led", HTTP_POST, handleDisplayTestLed);
  server.on("/display-test/pixel", HTTP_POST, handleDisplayTestPixel);
  server.on("/display-test/stop", HTTP_POST, handleDisplayTestStop);
  server.on("/status", HTTP_GET, serveStatus);
  server.on("/config", HTTP_GET, serveConfig);
  server.on("/save", HTTP_POST, saveNetwork);
  server.on("/config", HTTP_POST, saveConfig);
  server.on("/update", HTTP_GET, serveUpdatePage);
  server.on("/ota/check", HTTP_POST, handleOtaCheckNow);
  server.on("/ota/status", HTTP_GET, serveOtaStatus);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
  server.onNotFound(redirectToPortal);
}

void runNetworkStateMachine() {
  logWiFiStatus();
  if (state == CONNECTING) {
    if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAt >= NETWORK_RECONNECT_RETRY_MS) {
      lastReconnectAt = millis();
      Serial.println("[NET] Retrying saved Wi-Fi connection");
      WiFi.reconnect();
    }
    bool holdForFirstBoot = firstBootConnectHeld &&
                            millis() - networkTaskStartedAt < NETWORK_FIRST_CONNECT_MIN_MS;
    if (WiFi.status() == WL_CONNECTED && !holdForFirstBoot) {
      firstBootConnectHeld = false;
      enterOnline();
    } else if (millis() - stateStartedAt >= NETWORK_CONNECT_AND_PROBE_TIMEOUT_MS) {
      enterProvisioning();
    }
  } else if (state == ONLINE) {
    if (WiFi.status() == WL_CONNECTED) {
      disconnectStartedAt = 0;
    } else {
      if (disconnectStartedAt == 0) {
        disconnectStartedAt = millis();
      } else if (millis() - disconnectStartedAt >= NETWORK_RECONNECT_GRACE_MS) {
        Serial.println("[NET] Connection lost; retrying saved network");
        setupScreenVisible = true;
        tryReconnectWithSavedNetwork();
      }
    }
    if (setupScreenVisible && millis() - onlineAt >= NETWORK_SETUP_SCREEN_MS) {
      setupScreenVisible = false;
      releasePending = true;
    }
  } else if (state == PROVISIONING) {
    if (!savedSsid.isEmpty() && millis() - stateStartedAt >= NETWORK_PROVISIONING_RETRY_MS) {
      Serial.println("[NET] Retrying saved Wi-Fi after provisioning timeout");
      tryReconnectWithSavedNetwork();
    }
  }
}
} // namespace

bool isOnline() {
  return state == ONLINE && WiFi.status() == WL_CONNECTED;
}

bool isProvisioning() {
  return state == PROVISIONING;
}

bool portalEngaged() {
  return millis() < portalActiveUntil;
}

const char* getSavedWifiSsid() {
  return savedSsid.c_str();
}

String getDeviceIp() {
  return (state == ONLINE) ? WiFi.localIP().toString() : String("");
}

bool isClockDisplayEnabled() {
  return clockDisplayEnabled;
}

// Effective display timezone as a POSIX TZ string ("EST5EDT,M3.2.0,M11.1.0").
// Selected in the setup portal, persisted in NVS ("tz"); the factory default
// only applies before the first selection is saved.
const char* getTzString() {
  return tzString.c_str();
}

void getPreferredTeamIds(int outTeamIds[3]) {
  outTeamIds[0] = prefTeam1;
  outTeamIds[1] = prefTeam2;
  outTeamIds[2] = prefTeam3;
}


void startNetworkServices(const NetworkBranding& branding,
                          const NetworkTeamOption* teamOptions,
                          size_t teamOptionCount,
                          const int defaultPreferredTeams[3]) {
  netBranding = branding;
  netTeamOptions = teamOptions;
  netTeamOptionCount = teamOptionCount;
  netDefaultTeams[0] = defaultPreferredTeams[0];
  netDefaultTeams[1] = defaultPreferredTeams[1];
  netDefaultTeams[2] = defaultPreferredTeams[2];
  deviceHostname = branding.hostname;
  apSsid = branding.apSsid;
  networkTaskStartedAt = millis();
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS unavailable; runtime settings disabled");
  }
  registerPortalRoutes();
  WiFi.mode(WIFI_AP_STA);
  // Keep the modem awake from the very first connection (previously only
  // reconnects did this): default power save makes the radio sleep between
  // beacons, which stalls inbound portal page loads from phones.
  WiFi.setSleep(false);
  String macAddress = WiFi.macAddress();
  macAddress.replace(":", "");
  if (macAddress.length() >= 5) {
    // Per-device hostname off the branding base (e.g. "miniboard-ab12c")
    // so several boards can run side by side.
    deviceHostname = String(branding.hostname) + "-" +
                     macAddress.substring(macAddress.length() - 5);
    deviceHostname.toLowerCase();
  }
  WiFi.setHostname(deviceHostname.c_str());
  Serial.printf("[NET] Device hostname: %s\n", deviceHostname.c_str());
  // Keep the setup AP and captive portal available while trying saved Wi-Fi
  // credentials. It is stopped by enterOnline() once the station is connected,
  // so phones can still configure a board that has not joined a network yet.
  server.begin();
  setupScreenVisible = true;

  loadSavedNetwork();
  if (savedSsid.isEmpty()) {
    enterProvisioning();
  } else {
    if (ensureSetupAccessPoint()) {
      printSetupInstructions(WiFi.softAPIP().toString(), false);
    }
    WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
    enterConnecting();
  }
}

void handleNetworkServices() {
  if (dnsRunning) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  if (otaStarted) {
    ArduinoOTA.handle();
  }
  refreshScanCache();
  runNetworkStateMachine();
}

void handleNetworkDisplay() {
  if (redrawSetupPending) {
    redrawSetupPending = false;
    IPAddress ip(setupIpV);
    switch (redrawModeV) {
      case SETUP_CONNECTING:
      case SETUP_ONLINE_PORTAL:
        // Connecting and online transitions print nothing: the connecting
        // state is already logged by the network state machine, and the
        // online transition logs "[NET] Online: ip=...". Only the AP
        // provisioning instructions are worth a dedicated banner.
        break;
      case SETUP_AP_INSTRUCTIONS:
        printSetupInstructions(ip.toString(), false);
        break;
    }
  }
}

bool consumeScoreboardRelease() {
  if (!releasePending) {
    return false;
  }
  releasePending = false;
  return true;
}

void startNetworkTask() {
  xTaskCreatePinnedToCore(
    [](void*) {
      while (true) {
        handleNetworkServices();
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    },
    "NetworkTask",
    8192,
    nullptr,
    2,  // above the MLB data task: the portal must preempt feed fetches
    nullptr,
    0
  );
}
