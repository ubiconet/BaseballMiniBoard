#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>

#include <stdlib.h>
#include <stdio.h>

#include "common/data/snapshot_channel.h"
#include "manual_control.h"

namespace {
const uint8_t STATE_SCHEMA = 1;
const char* STATE_NAMESPACE = "manual";
const char* STATE_KEY = "game";
const uint8_t MAX_SCORE = 99;
const uint8_t MAX_INNING = 99;

struct PersistedGameState {
  uint8_t schema;
  uint8_t homeScore;
  uint8_t awayScore;
  uint8_t inning;
  uint8_t topHalf;
  uint8_t balls;
  uint8_t strikes;
  uint8_t outs;
};
static_assert(sizeof(PersistedGameState) == 8,
              "Persisted game state must remain a compact byte record");

SnapshotChannel<ManualGameState> gameStateChannel;
ManualGameState currentState = {1, 0, 0, 1, 1, 0, 0, 0};
WebServer* portalServer = nullptr;

PersistedGameState toRecord(const ManualGameState& state) {
  return PersistedGameState{STATE_SCHEMA, state.homeScore, state.awayScore,
                            state.inning, state.topHalf, state.balls,
                            state.strikes, state.outs};
}

bool validRecord(const PersistedGameState& record) {
  return record.schema == STATE_SCHEMA &&
         record.homeScore <= MAX_SCORE &&
         record.awayScore <= MAX_SCORE &&
         record.inning >= 1 && record.inning <= MAX_INNING &&
         record.topHalf <= 1 && record.balls <= 3 &&
         record.strikes <= 2 && record.outs <= 2;
}

bool persistState(const ManualGameState& state) {
  const PersistedGameState record = toRecord(state);
  Preferences storage;
  if (!storage.begin(STATE_NAMESPACE, false)) {
    Serial.println("[MANUAL] ERROR: unable to open state storage");
    return false;
  }
  const size_t written = storage.putBytes(STATE_KEY, &record, sizeof(record));
  storage.end();
  if (written != sizeof(record)) {
    Serial.printf("[MANUAL] ERROR: saved %u of %u state bytes\n",
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(sizeof(record)));
    return false;
  }
  return true;
}

bool commitState(const ManualGameState& next) {
  if (!persistState(next)) return false;
  currentState = next;
  currentState.valid = 1;
  gameStateChannel.publish(currentState);
  return true;
}

bool parseInteger(const char* name, int& value) {
  if (!portalServer->hasArg(name)) return false;
  const String argument = portalServer->arg(name);
  if (argument.isEmpty()) return false;
  char* end = nullptr;
  const long parsed = strtol(argument.c_str(), &end, 10);
  if (end == argument.c_str() || *end != '\0' ||
      parsed < -1 || parsed > MAX_SCORE) {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

void sendState() {
  char body[128];
  snprintf(body, sizeof(body),
           "{\"home\":%u,\"away\":%u,\"inning\":%u,\"half\":\"%s\","
           "\"balls\":%u,\"strikes\":%u,\"outs\":%u}",
           currentState.homeScore, currentState.awayScore,
           currentState.inning, currentState.topHalf ? "top" : "bottom",
           currentState.balls, currentState.strikes, currentState.outs);
  portalServer->send(200, "application/json", body);
}

void sendPersistenceError() {
  portalServer->send(500, "text/plain", "Unable to save manual game state");
}

void serveManualPage() {
  portalServer->send(200, "text/html", R"html(<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Manual Controls</title><style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:#111526;color:#eef2ff;font:15px system-ui,sans-serif}
main{max-width:1100px;margin:20px auto;padding:16px}
h1{margin:0 0 16px;font-size:22px}
.card{margin:0 0 16px;padding:16px;background:#172441;border:1px solid #2c426d;border-radius:10px}
h2{margin:0 0 14px;color:#91a7d2;font-size:14px;letter-spacing:.04em;font-weight:600}
.scores{display:grid;grid-template-columns:1fr 1fr;gap:16px}
.team{padding:14px;background:#171a2c;border-radius:9px;text-align:center}
.team-name{display:block;margin-bottom:8px;color:#9aadd2;font-size:13px}
.stepper{display:flex;align-items:center;justify-content:center;gap:14px}
button{min-width:44px;min-height:44px;border:1px solid #2b416c;border-radius:7px;background:#172441;color:#eef2ff;font:inherit;cursor:pointer}
button:hover{border-color:#6882b3}
.score-value,.inning-value{min-width:36px;text-align:center;font-size:29px;font-weight:750}
.inning-row{display:flex;align-items:center;justify-content:space-between;gap:18px}
.half-buttons{display:flex;gap:8px}
.half-buttons button{min-width:90px}
.half-buttons .selected{background:#ed405f;border-color:#ed405f;color:white;font-weight:700}
.count-groups{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;text-align:center}
.count-title{display:block;margin:0 0 8px;color:#9aadd2;font-size:13px}
.dots{display:flex;justify-content:center;gap:8px}
.dot{min-width:28px;width:28px;min-height:28px;height:28px;padding:0;border:2px solid #2b416c;border-radius:50%;background:transparent}
.dot.active{background:#ed405f;border-color:#ed405f}
.actions{display:flex;justify-content:center;gap:8px;margin-top:16px}
.actions button{padding:0 14px}
.message{min-height:20px;margin:8px 0;color:#ffb4bf;text-align:center}
.back{display:inline-block;margin:0 0 16px;color:#f5c400}
@media(max-width:620px){main{margin:0 auto;padding:10px}.scores{grid-template-columns:1fr;gap:10px}.inning-row{align-items:flex-start;flex-direction:column}.half-buttons{width:100%}.half-buttons button{flex:1}.count-groups{gap:4px}.dots{gap:4px}.dot{min-width:24px;width:24px;min-height:24px;height:24px}}
</style></head><body><main><a class="back" href="/">Back to settings</a>
<h1>Manual Controls</h1>
<section class="card"><h2>SCORE</h2><div class="scores">
<div class="team"><span class="team-name">HOME</span><div class="stepper">
<button data-score="home" data-delta="-1" aria-label="Decrease home score">−</button><strong class="score-value" id="homeScore">0</strong><button data-score="home" data-delta="1" aria-label="Increase home score">+</button>
</div></div>
<div class="team"><span class="team-name">AWAY</span><div class="stepper">
<button data-score="away" data-delta="-1" aria-label="Decrease away score">−</button><strong class="score-value" id="awayScore">0</strong><button data-score="away" data-delta="1" aria-label="Increase away score">+</button>
</div></div></div></section>
<section class="card"><h2>INNING</h2><div class="inning-row">
<div class="stepper"><button data-inning="-1" aria-label="Previous inning">−</button><strong class="inning-value" id="inning">1</strong><button data-inning="1" aria-label="Next inning">+</button></div>
<div class="half-buttons"><button id="topButton">Top</button><button id="bottomButton">Bottom</button></div>
</div></section>
<section class="card"><h2>COUNT</h2><div class="count-groups">
<div><span class="count-title">Balls</span><div class="dots" id="balls"></div></div>
<div><span class="count-title">Strikes</span><div class="dots" id="strikes"></div></div>
<div><span class="count-title">Outs</span><div class="dots" id="outs"></div></div>
</div><div class="actions"><button id="resetCount">Reset Count</button><button id="resetAll">Reset All</button></div>
<p class="message" id="message" role="status" aria-live="polite"></p></section></main>
<script>
var state=null;
function render(){if(!state)return;
document.getElementById('homeScore').textContent=state.home;
document.getElementById('awayScore').textContent=state.away;
document.getElementById('inning').textContent=state.inning;
document.getElementById('topButton').className=state.half==='top'?'selected':'';
document.getElementById('bottomButton').className=state.half==='bottom'?'selected':'';
[['balls',state.balls,3],['strikes',state.strikes,2],['outs',state.outs,2]].forEach(function(group){
var root=document.getElementById(group[0]);root.textContent='';
for(var i=0;i<group[2];i++){var b=document.createElement('button'),on=group[1]>i;
b.className='dot'+(on?' active':'');b.setAttribute('aria-label',group[0]+' '+(i+1));b.setAttribute('aria-pressed',on?'true':'false');
b.onclick=function(name,index){return function(){send('/manual/count',{group:name,index:index})}}(group[0],i);root.appendChild(b)}
});
}
function send(path,data){document.getElementById('message').textContent='';
return fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)})
.then(function(r){if(!r.ok)return r.text().then(function(t){throw new Error(t||'Request failed')});return r.json()})
.then(function(s){state=s;render()}).catch(function(e){document.getElementById('message').textContent=e.message})
}
document.querySelectorAll('[data-score]').forEach(function(b){b.onclick=function(){send('/manual/score',{team:b.getAttribute('data-score'),delta:b.getAttribute('data-delta')})}});
document.querySelectorAll('[data-inning]').forEach(function(b){b.onclick=function(){send('/manual/inning',{delta:b.getAttribute('data-inning')})}});
document.getElementById('topButton').onclick=function(){send('/manual/half',{half:'top'})};
document.getElementById('bottomButton').onclick=function(){send('/manual/half',{half:'bottom'})};
document.getElementById('resetCount').onclick=function(){send('/manual/reset-count',{})};
document.getElementById('resetAll').onclick=function(){send('/manual/reset-all',{})};
fetch('/manual/state').then(function(r){if(!r.ok)throw new Error('Unable to load game state');return r.json()})
.then(function(s){state=s;render()}).catch(function(e){document.getElementById('message').textContent=e.message});
</script></body></html>)html");
}

void handleManualState() {
  sendState();
}

void handleScoreChange() {
  int delta = 0;
  const String team = portalServer->arg("team");
  if (!parseInteger("delta", delta) || (delta != -1 && delta != 1) ||
      (team != "home" && team != "away")) {
    portalServer->send(400, "text/plain",
                       "Expected team=home|away and delta=-1|1");
    return;
  }
  ManualGameState next = currentState;
  uint8_t& score = team == "home" ? next.homeScore : next.awayScore;
  if (delta > 0 && score < MAX_SCORE) ++score;
  if (delta < 0 && score > 0) --score;
  if (!commitState(next)) {
    sendPersistenceError();
    return;
  }
  sendState();
}

void handleInningChange() {
  int delta = 0;
  if (!parseInteger("delta", delta) || (delta != -1 && delta != 1)) {
    portalServer->send(400, "text/plain", "Expected delta=-1|1");
    return;
  }
  ManualGameState next = currentState;
  if (delta > 0 && next.inning < MAX_INNING) ++next.inning;
  if (delta < 0 && next.inning > 1) --next.inning;
  if (!commitState(next)) {
    sendPersistenceError();
    return;
  }
  sendState();
}

void handleHalfChange() {
  const String half = portalServer->arg("half");
  if (half != "top" && half != "bottom") {
    portalServer->send(400, "text/plain", "Expected half=top|bottom");
    return;
  }
  ManualGameState next = currentState;
  next.topHalf = half == "top" ? 1 : 0;
  if (!commitState(next)) {
    sendPersistenceError();
    return;
  }
  sendState();
}

void handleCountChange() {
  const String group = portalServer->arg("group");
  int index = 0;
  if (!parseInteger("index", index) || index < 0 || index > 2) {
    portalServer->send(400, "text/plain",
                       "Expected count group and index=0..2");
    return;
  }
  int maximum = 0;
  if (group == "balls") {
    maximum = 3;
  } else if (group == "strikes") {
    maximum = 2;
  } else if (group == "outs") {
    maximum = 2;
  } else {
    portalServer->send(400, "text/plain",
                       "Expected group=balls|strikes|outs");
    return;
  }
  if (index >= maximum) {
    portalServer->send(400, "text/plain", "Count index is out of range");
    return;
  }
  ManualGameState next = currentState;
  uint8_t& nextCount = group == "balls" ? next.balls :
                       (group == "strikes" ? next.strikes : next.outs);
  nextCount = nextCount == index + 1 ? index : index + 1;
  if (!commitState(next)) {
    sendPersistenceError();
    return;
  }
  sendState();
}

void handleResetCount() {
  ManualGameState next = currentState;
  next.balls = 0;
  next.strikes = 0;
  next.outs = 0;
  if (!commitState(next)) {
    sendPersistenceError();
    return;
  }
  sendState();
}

void handleResetAll() {
  const ManualGameState reset = {1, 0, 0, 1, 1, 0, 0, 0};
  if (!commitState(reset)) {
    sendPersistenceError();
    return;
  }
  sendState();
}
}  // namespace

void initializeManualGameState() {
  currentState = {1, 0, 0, 1, 1, 0, 0, 0};
  Preferences storage;
  if (!storage.begin(STATE_NAMESPACE, true)) {
    Serial.println("[MANUAL] ERROR: unable to read saved game state");
  } else {
    const size_t length = storage.getBytesLength(STATE_KEY);
    PersistedGameState record{};
    const size_t read = length == sizeof(record)
        ? storage.getBytes(STATE_KEY, &record, sizeof(record)) : 0;
    if (read == sizeof(record) && validRecord(record)) {
      currentState.homeScore = record.homeScore;
      currentState.awayScore = record.awayScore;
      currentState.inning = record.inning;
      currentState.topHalf = record.topHalf;
      currentState.balls = record.balls;
      currentState.strikes = record.strikes;
      currentState.outs = record.outs;
      Serial.println("[MANUAL] Restored saved game state");
    } else if (length != 0) {
      Serial.println("[MANUAL] WARNING: saved state is invalid; using defaults");
    }
    storage.end();
  }
  gameStateChannel.publish(currentState);
}

bool takeManualGameState(ManualGameState& state, uint32_t& lastGeneration) {
  return gameStateChannel.take(state, lastGeneration);
}

void registerManualControlRoutes(WebServer& portal) {
  portalServer = &portal;
  portal.on("/manual", HTTP_GET, serveManualPage);
  portal.on("/manual/state", HTTP_GET, handleManualState);
  portal.on("/manual/score", HTTP_POST, handleScoreChange);
  portal.on("/manual/inning", HTTP_POST, handleInningChange);
  portal.on("/manual/half", HTTP_POST, handleHalfChange);
  portal.on("/manual/count", HTTP_POST, handleCountChange);
  portal.on("/manual/reset-count", HTTP_POST, handleResetCount);
  portal.on("/manual/reset-all", HTTP_POST, handleResetAll);
}
