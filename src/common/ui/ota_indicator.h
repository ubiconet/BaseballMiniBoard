#pragma once

#include <Arduino.h>

// Firmware self-update indicator (driven by the OTA updater on core 0,
// drawn by handleOtaIndicator() on the render loop). This build has no
// TFT: an update shows "UD" on the home matrix while all seven count LEDs
// flash together. Only POD + a small string cross the task boundary.

enum class OtaStage : uint8_t {
  NONE = 0,
  DOWNLOADING,   // binary is streaming to flash (progress 0..100)
  REBOOTING,     // image verified, device about to restart
  FAILED,        // download/flash failed; normal operation resumes
};

void publishOtaStage(OtaStage stage, int progressPct);  // core-0 updater
OtaStage getOtaStage();
int getOtaProgress();
void setOtaTargetVersion(const char* version);
// Paints the update state and flashes the count LEDs while an update is in
// progress. Returns true when the indicator owns this frame so the caller
// (loop()) should skip normal rendering.
bool handleOtaIndicator();
