#pragma once

#include <Arduino.h>

struct DisplayTestState {
  bool valid;
  bool active;
  uint8_t ledMask;
  uint64_t matrixPixels[3];
};

// Network-task controls. These only publish a POD request; hardware remains
// owned by the render loop.
DisplayTestState getRequestedDisplayTestState();
bool setDisplayTestLed(uint8_t index, bool enabled);
bool setDisplayTestPixel(uint8_t matrixIndex, uint8_t x, uint8_t y,
                         bool enabled);
void stopDisplayTest();

// Applies the latest test state on the render loop. Returns true while the
// test owns the display.
bool handleDisplayTest();
