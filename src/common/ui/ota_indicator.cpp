#include <Arduino.h>

#include "config.h"
#include "common/hal/count_leds.h"
#include "common/hal/led_matrix.h"
#include "common/ui/ota_indicator.h"

namespace {
volatile OtaStage gOtaStage = OtaStage::NONE;
volatile int gOtaProgress = 0;
char gOtaTargetVersion[16] = "";

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
  static bool sIndicatorActive = false;
  static OtaStage sLastStage = OtaStage::NONE;
  static uint32_t sLastBlinkAt = 0;
  static bool sBlinkOn = true;

  OtaStage stage = gOtaStage;
  if (stage == OtaStage::NONE) {
    if (sIndicatorActive) {
      setMax7219UpdateNotice(false);
      setCountLedsOverride(false, 0);
      sIndicatorActive = false;
      sLastStage = OtaStage::NONE;
    }
    return false;
  }

  if (!sIndicatorActive) {
    setMax7219UpdateNotice(true);
    setCountLedsOverride(true, 0x7F);
    sIndicatorActive = true;
    sBlinkOn = true;
    sLastBlinkAt = millis();
  } else if (millis() - sLastBlinkAt >= OTA_BLINK_STEP_MS) {
    sLastBlinkAt = millis();
    sBlinkOn = !sBlinkOn;
    setCountLedsOverride(true, sBlinkOn ? 0x7F : 0);
  }

  if (stage != sLastStage) {
    sLastStage = stage;
    if (stage == OtaStage::REBOOTING) {
      DBG_PRINTF("[OTA] update %s installed; rebooting...\n",
                 gOtaTargetVersion);
    }
  }
  return true;
}
