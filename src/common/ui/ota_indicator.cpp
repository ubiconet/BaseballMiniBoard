#include <Arduino.h>

#include "config.h"
#include "common/hal/led_matrix.h"
#include "common/ui/ota_indicator.h"

namespace {
volatile OtaStage gOtaStage = OtaStage::NONE;
volatile int gOtaProgress = 0;
char gOtaTargetVersion[16] = "";

const int OTA_BAR_COLUMNS = 16;  // both 8x8 matrices side by side
const uint32_t OTA_BLINK_STEP_MS = 250;
}  // namespace

void publishOtaStage(OtaStage stage, int progressPct) {
  gOtaProgress = progressPct;
  __sync_synchronize();
  gOtaStage = stage;
  DBG_PRINTF("[OTA] stage=%d progress=%d%%\n", (int)stage, progressPct);
}
OtaStage getOtaStage() { return gOtaStage; }
int getOtaProgress() { return gOtaProgress; }
void setOtaTargetVersion(const char* version) {
  if (version == nullptr) return;
  strlcpy(gOtaTargetVersion, version, sizeof(gOtaTargetVersion));
}

bool handleOtaIndicator() {
  if (gOtaStage == OtaStage::NONE) return false;

  static OtaStage sLastDrawnStage = OtaStage::NONE;
  static int sLastDrawnPct = -1;
  static uint32_t sLastBlinkAt = 0;
  static bool sBlinkOn = true;

  if (gOtaStage == OtaStage::DOWNLOADING) {
    // 16 columns total: bar length tracks the download percentage.
    if (gOtaStage != sLastDrawnStage || gOtaProgress != sLastDrawnPct) {
      sLastDrawnStage = gOtaStage;
      sLastDrawnPct = gOtaProgress;
      setMax7219Bar(gOtaProgress * OTA_BAR_COLUMNS / 100);
    }
    return true;
  }

  if (gOtaStage != sLastDrawnStage) {
    sLastDrawnStage = gOtaStage;
    sLastDrawnPct = -1;
    if (gOtaStage == OtaStage::REBOOTING) {
      DBG_PRINTF("[OTA] update %s installed; rebooting...\n",
                 gOtaTargetVersion);
    }
  }
  if (gOtaStage == OtaStage::REBOOTING) {
    setMax7219Bar(OTA_BAR_COLUMNS);  // solid: done, about to restart
    return true;
  }

  // FAILED: blink everything for the updater's notice window, then the
  // updater republishes NONE and the normal display takes over again.
  if (millis() - sLastBlinkAt >= OTA_BLINK_STEP_MS) {
    sLastBlinkAt = millis();
    sBlinkOn = !sBlinkOn;
    setMax7219Bar(sBlinkOn ? OTA_BAR_COLUMNS : 0);
  }
  return true;
}
