#include <Arduino.h>

#include "count_leds.h"

namespace {
int COUNT_LED_PINS[7] = {-1, -1, -1, -1, -1, -1, -1};
uint8_t desiredCountA = 0;
uint8_t desiredCountB = 0;
uint8_t desiredCountC = 0;
bool countLedsOverridden = false;
uint8_t countLedsOverrideMask = 0;

void writeCountLeds(uint8_t countA, uint8_t countB, uint8_t countC) {
  digitalWrite(COUNT_LED_PINS[0], countA >= 1 ? HIGH : LOW);
  digitalWrite(COUNT_LED_PINS[1], countA >= 2 ? HIGH : LOW);
  digitalWrite(COUNT_LED_PINS[2], countA >= 3 ? HIGH : LOW);

  digitalWrite(COUNT_LED_PINS[3], countB >= 1 ? HIGH : LOW);
  digitalWrite(COUNT_LED_PINS[4], countB >= 2 ? HIGH : LOW);

  digitalWrite(COUNT_LED_PINS[5], countC >= 1 ? HIGH : LOW);
  digitalWrite(COUNT_LED_PINS[6], countC >= 2 ? HIGH : LOW);
}

void writeCountLedMask(uint8_t ledMask) {
  for (uint8_t i = 0; i < 7; ++i) {
    digitalWrite(COUNT_LED_PINS[i], (ledMask & (1U << i)) ? HIGH : LOW);
  }
}
}

void initCountLeds(const int pins[7]) {
  for (int i = 0; i < 7; ++i) {
    COUNT_LED_PINS[i] = pins[i];
    pinMode(pins[i], OUTPUT);
  }
  setCountLeds(0, 0, 0);
  Serial.println("[HW] Count LED driver initialized");
}

void setCountLeds(uint8_t countA, uint8_t countB, uint8_t countC) {
  desiredCountA = countA;
  desiredCountB = countB;
  desiredCountC = countC;
  if (!countLedsOverridden) {
    writeCountLeds(desiredCountA, desiredCountB, desiredCountC);
  }
}

void setCountLedsOverride(bool enabled, uint8_t ledMask) {
  if (enabled) {
    if (!countLedsOverridden || countLedsOverrideMask != ledMask) {
      writeCountLedMask(ledMask);
      countLedsOverrideMask = ledMask;
    }
    countLedsOverridden = true;
  } else if (countLedsOverridden) {
    countLedsOverridden = false;
    writeCountLeds(desiredCountA, desiredCountB, desiredCountC);
  }
}

void runCountLedTestLoop() {
  for (int i = 0; i < 7; ++i) {
    for (int j = 0; j < 7; ++j) {
      digitalWrite(COUNT_LED_PINS[j], j == i ? HIGH : LOW);
    }

    Serial.printf("[HW TEST] LED pin %d ON\n", COUNT_LED_PINS[i]);
    delay(500);
  }

  setCountLeds(0, 0, 0);
}
