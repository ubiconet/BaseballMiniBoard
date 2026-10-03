#include <Arduino.h>

#include "common/data/snapshot_channel.h"
#include "common/hal/count_leds.h"
#include "common/hal/led_matrix.h"
#include "display_test.h"

namespace {
SnapshotChannel<DisplayTestState> gDisplayTestChannel;
DisplayTestState gRequestedState = {true, false, 0, 0, 0};
DisplayTestState gAppliedState = {false, false, 0, 0, 0};
uint32_t gLastGeneration = 0;
bool gWasActive = false;

void publishDisplayTestState() {
  gRequestedState.valid = true;
  gDisplayTestChannel.publish(gRequestedState);
}
}  // namespace

DisplayTestState getRequestedDisplayTestState() {
  return gRequestedState;
}

bool setDisplayTestLed(uint8_t index, bool enabled) {
  if (index >= 7) return false;
  uint8_t bit = static_cast<uint8_t>(1U << index);
  if (enabled) gRequestedState.ledMask |= bit;
  else gRequestedState.ledMask &= static_cast<uint8_t>(~bit);
  gRequestedState.active = true;
  publishDisplayTestState();
  return true;
}

bool setDisplayTestPixel(bool homeMatrix, uint8_t x, uint8_t y, bool enabled) {
  if (x >= 8 || y >= 8) return false;
  uint64_t bit = static_cast<uint64_t>(1) << (y * 8 + x);
  uint64_t& pixels = homeMatrix ? gRequestedState.homePixels
                                : gRequestedState.awayPixels;
  if (enabled) pixels |= bit;
  else pixels &= ~bit;
  gRequestedState.active = true;
  publishDisplayTestState();
  return true;
}

void stopDisplayTest() {
  gRequestedState.active = false;
  gRequestedState.ledMask = 0;
  gRequestedState.awayPixels = 0;
  gRequestedState.homePixels = 0;
  publishDisplayTestState();
}

bool handleDisplayTest() {
  DisplayTestState next{};
  if (gDisplayTestChannel.take(next, gLastGeneration)) {
    gAppliedState = next;
  }

  if (!gAppliedState.active) {
    if (gWasActive) {
      setCountLedsOverride(false, 0);
      setMax7219PixelsOverride(false, 0, 0);
      gWasActive = false;
    }
    return false;
  }

  setCountLedsOverride(true, gAppliedState.ledMask);
  setMax7219PixelsOverride(true, gAppliedState.awayPixels,
                           gAppliedState.homePixels);
  gWasActive = true;
  return true;
}
