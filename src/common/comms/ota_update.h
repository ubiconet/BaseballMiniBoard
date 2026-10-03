#pragma once

#include <Arduino.h>

// Firmware self-update against the releases/manifest.json in the GitHub
// repo (written by `pio run -t deploy`). Runs on the core-0 update task:
// fetch manifest, compare version with FIRMWARE_VERSION, and when
// they differ download + flash the new binary while the center matrix shows
// "UD" and the count LEDs flash (see handleOtaIndicator()).
// onlineForMs = milliseconds since the network last came online (0 while
// offline); the first check fires shortly after association.
void serviceOtaUpdates(uint32_t onlineForMs);

// Ask the update task to run a check, bypassing the retry throttle.
void requestOtaCheckNow();
bool otaEverChecked();         // at least one check completed
bool otaLastCheckOk();         // last manifest check/update download had no error
