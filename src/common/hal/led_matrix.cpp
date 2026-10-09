#include <Arduino.h>
#include <string.h>

#include "led_matrix.h"

namespace {
// Pins, captured from initLedMatrix() — the driver never reads config itself.
int DIN_PIN = -1;
int CLK_PIN = -1;
int CS_PIN = -1;

// MAX7219 Register Addresses
const uint8_t MAX7219_REG_NOOP = 0x00;
const uint8_t MAX7219_REG_DIGIT0 = 0x01;
const uint8_t MAX7219_REG_DECODEMODE = 0x09;
const uint8_t MAX7219_REG_INTENSITY = 0x0A;
const uint8_t MAX7219_REG_SCANLIMIT = 0x0B;
const uint8_t MAX7219_REG_SHUTDOWN = 0x0C;
const uint8_t MAX7219_REG_DISPLAYTEST = 0x0F;

// 3x5 font digits for 2-digit scores (0-9)
const uint8_t FONT_3X5[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
  {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
  {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
  {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
  {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
  {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
  {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
  {0b111, 0b001, 0b010, 0b010, 0b010}, // 7
  {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
  {0b111, 0b101, 0b111, 0b001, 0b111}  // 9
};
const uint8_t LETTER_D_3X5[5] = {0b110, 0b101, 0b101, 0b101, 0b110};
const uint8_t LETTER_U_3X5[5] = {0b101, 0b101, 0b101, 0b101, 0b111};

// 5x7 font digits for 1-digit centered scores (0-9)
const uint8_t FONT_5X7[10][7] = {
  {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // 0
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}, // 2
  {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}, // 3
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}, // 4
  {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}, // 5
  {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}, // 6
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}, // 7
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}, // 8
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}  // 9
};

// 5x7 font letters used only for the boot-time matrix test.
const uint8_t LETTER_H_5X7[7] = {
  0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001
};
const uint8_t LETTER_A_5X7[7] = {
  0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001
};

const uint8_t MATRIX_GAME_INTENSITY = 0x05;
uint8_t matrixIntensity = MATRIX_GAME_INTENSITY;
uint8_t displayedAwayRows[8] = {0};
uint8_t displayedCenterRows[8] = {0};
uint8_t displayedHomeRows[8] = {0};
uint8_t savedAwayRows[8] = {0};
uint8_t savedCenterRows[8] = {0};
uint8_t savedHomeRows[8] = {0};
bool hasDisplayedRows = false;
bool matrixOverrideActive = false;
uint64_t matrixOverrideAwayPixels = 0;
uint64_t matrixOverrideCenterPixels = 0;
uint64_t matrixOverrideHomePixels = 0;
uint8_t savedMatrixIntensity = MATRIX_GAME_INTENSITY;

void max7219ShiftByte(uint8_t data) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(CLK_PIN, LOW);
    digitalWrite(DIN_PIN, (data & (1 << i)) ? HIGH : LOW);
    digitalWrite(CLK_PIN, HIGH);
  }
}

// Arguments use device order left, center, right; cascaded modules are shifted
// in reverse device order so the final pair reaches device 0.
void max7219Send(uint8_t reg0, uint8_t data0,
                 uint8_t reg1, uint8_t data1,
                 uint8_t reg2, uint8_t data2) {
  digitalWrite(CS_PIN, LOW);
  max7219ShiftByte(reg2);
  max7219ShiftByte(data2);
  max7219ShiftByte(reg1);
  max7219ShiftByte(data1);
  max7219ShiftByte(reg0);
  max7219ShiftByte(data0);
  digitalWrite(CS_PIN, HIGH);
}

void max7219SendAll(uint8_t reg, uint8_t data) {
  max7219Send(reg, data, reg, data, reg, data);
}

void setMatrixIntensity(uint8_t intensity) {
  if (matrixIntensity == intensity) return;
  max7219SendAll(MAX7219_REG_INTENSITY, intensity);
  matrixIntensity = intensity;
}

// Bit c of row r ("column c") lives at bit position (7-c) of rows[r].
uint8_t getMatrixBit(const uint8_t rows[8], int r, int c) {
  return (rows[r] >> (7 - c)) & 0x01;
}

void setMatrixBit(uint8_t rows[8], int r, int c, uint8_t value) {
  if (value) {
    rows[r] |= static_cast<uint8_t>(1 << (7 - c));
  } else {
    rows[r] &= static_cast<uint8_t>(~(1 << (7 - c)));
  }
}

// Convert integer score (0-99) into 8 row bytes for an 8x8 matrix. A negative
// score (MAX7219_SCORE_BLANK) leaves the matrix dark.
void scoreToMatrixRows(int score, uint8_t rows[8]) {
  for (int i = 0; i < 8; i++) rows[i] = 0;
  if (score < 0) return;
  if (score > 99) score = 99;

  if (score < 10) {
    for (int r = 0; r < 7; r++) {
      rows[r] = (FONT_5X7[score][r] & 0x1F) << 1; // Center 5 bits in 8 cols
    }
  } else {
    // Two 3x5 digits side by side: tens at cols 7..5, ones at cols 3..1.
    int tens = score / 10;
    int ones = score % 10;
    for (int r = 0; r < 5; r++) {
      uint8_t tensBits = FONT_3X5[tens][r] & 0x07;
      uint8_t onesBits = FONT_3X5[ones][r] & 0x07;
      rows[r + 1] = (tensBits << 4) | onesBits;
    }
  }
}

void writeMatrixRows(const uint8_t awayRows[8],
                     const uint8_t centerRows[8],
                     const uint8_t homeRows[8]) {
  if (!matrixOverrideActive) {
    memcpy(displayedAwayRows, awayRows, sizeof(displayedAwayRows));
    memcpy(displayedCenterRows, centerRows, sizeof(displayedCenterRows));
    memcpy(displayedHomeRows, homeRows, sizeof(displayedHomeRows));
    hasDisplayedRows = true;
  }
  uint8_t awayOutput[8];
  uint8_t centerOutput[8];
  uint8_t homeOutput[8];
  memcpy(awayOutput, awayRows, sizeof(awayOutput));
  memcpy(centerOutput, centerRows, sizeof(centerOutput));
  memcpy(homeOutput, homeRows, sizeof(homeOutput));
  for (uint8_t row = 0; row < 8; row++) {
    uint8_t reg = MAX7219_REG_DIGIT0 + row;
    max7219Send(reg, awayOutput[row], reg, centerOutput[row],
                reg, homeOutput[row]);
  }
}
} // namespace

void initLedMatrix(int dinPin, int clkPin, int csPin) {
  DIN_PIN = dinPin;
  CLK_PIN = clkPin;
  CS_PIN = csPin;

  pinMode(DIN_PIN, OUTPUT);
  pinMode(CLK_PIN, OUTPUT);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  // Initialize MAX7219 registers
  max7219SendAll(MAX7219_REG_SHUTDOWN, 0x01);    // Normal operation
  max7219SendAll(MAX7219_REG_DECODEMODE, 0x00);  // Raw matrix mode
  max7219SendAll(MAX7219_REG_SCANLIMIT, 0x07);   // Scan all 8 digits
  max7219SendAll(MAX7219_REG_INTENSITY, MATRIX_GAME_INTENSITY);
  max7219SendAll(MAX7219_REG_DISPLAYTEST, 0x00); // Test off

  setMax7219Scores(MAX7219_SCORE_BLANK, MAX7219_SCORE_BLANK);
  Serial.println("[HW] MAX7219 matrix driver initialized");
}

void setMax7219Scores(int awayScore, int homeScore) {
  setMatrixIntensity(MATRIX_GAME_INTENSITY);
  uint8_t awayRows[8];
  uint8_t homeRows[8];

  scoreToMatrixRows(awayScore, awayRows);
  scoreToMatrixRows(homeScore, homeRows);
  writeMatrixRows(awayRows, displayedCenterRows, homeRows);
}

void setMax7219CenterRows(const uint8_t rows[8]) {
  setMatrixIntensity(MATRIX_GAME_INTENSITY);
  writeMatrixRows(displayedAwayRows, rows, displayedHomeRows);
}

void setMax7219PixelsOverride(bool enabled, uint64_t awayPixels,
                              uint64_t centerPixels, uint64_t homePixels) {
  if (enabled) {
    if (matrixOverrideActive &&
        awayPixels == matrixOverrideAwayPixels &&
        centerPixels == matrixOverrideCenterPixels &&
        homePixels == matrixOverrideHomePixels) {
      return;
    }
    if (!matrixOverrideActive) {
      memcpy(savedAwayRows, displayedAwayRows, sizeof(savedAwayRows));
      memcpy(savedCenterRows, displayedCenterRows, sizeof(savedCenterRows));
      memcpy(savedHomeRows, displayedHomeRows, sizeof(savedHomeRows));
      savedMatrixIntensity = matrixIntensity;
      matrixOverrideActive = true;
    }
    matrixOverrideAwayPixels = awayPixels;
    matrixOverrideCenterPixels = centerPixels;
    matrixOverrideHomePixels = homePixels;

    uint8_t awayRows[8] = {0};
    uint8_t centerRows[8] = {0};
    uint8_t homeRows[8] = {0};
    for (int row = 0; row < 8; ++row) {
      for (int column = 0; column < 8; ++column) {
        int pixel = row * 8 + column;
        setMatrixBit(awayRows, row, column,
                     (awayPixels >> pixel) & 1U);
        setMatrixBit(centerRows, row, column,
                     (centerPixels >> pixel) & 1U);
        setMatrixBit(homeRows, row, column,
                     (homePixels >> pixel) & 1U);
      }
    }
    setMatrixIntensity(MATRIX_GAME_INTENSITY);
    writeMatrixRows(awayRows, centerRows, homeRows);
    return;
  }

  if (!matrixOverrideActive) return;
  matrixOverrideActive = false;
  setMatrixIntensity(savedMatrixIntensity);
  if (hasDisplayedRows) {
    uint8_t awayRows[8];
    uint8_t centerRows[8];
    uint8_t homeRows[8];
    memcpy(awayRows, savedAwayRows, sizeof(awayRows));
    memcpy(centerRows, savedCenterRows, sizeof(centerRows));
    memcpy(homeRows, savedHomeRows, sizeof(homeRows));
    writeMatrixRows(awayRows, centerRows, homeRows);
  }
}

void setMax7219UpdateNotice(bool enabled) {
  uint64_t centerPixels = 0;
  if (enabled) {
    for (int row = 0; row < 5; ++row) {
      for (int column = 0; column < 3; ++column) {
        if (LETTER_U_3X5[row] & (1U << (2 - column))) {
          int pixel = (row + 1) * 8 + (column + 1);
          centerPixels |= (uint64_t)1 << pixel;
        }
        if (LETTER_D_3X5[row] & (1U << (2 - column))) {
          int pixel = (row + 1) * 8 + (column + 5);
          centerPixels |= (uint64_t)1 << pixel;
        }
      }
    }
  }
  setMax7219PixelsOverride(enabled, 0, centerPixels, 0);
}

void setMax7219Bar(int litColumns) {
  if (litColumns < 0)  litColumns = 0;
  if (litColumns > 24) litColumns = 24;
  setMatrixIntensity(MATRIX_GAME_INTENSITY);

  // Fill full-height columns of a logical 24-wide strip across all matrices.
  uint8_t awayRows[8] = {0};
  uint8_t centerRows[8] = {0};
  uint8_t homeRows[8] = {0};
  for (int c = 0; c < litColumns; ++c) {
    for (int r = 0; r < 8; ++r) {
      uint8_t* matrixRows = c < 8 ? awayRows : (c < 16 ? centerRows : homeRows);
      setMatrixBit(matrixRows, r, c & 7, 1);
    }
  }
  writeMatrixRows(awayRows, centerRows, homeRows);
}

void runMax7219BootTest() {
  uint8_t homeRows[8] = {0};
  uint8_t centerRows[8] = {0};
  uint8_t awayRows[8] = {0};
  for (int row = 0; row < 7; row++) {
    homeRows[row] = (LETTER_H_5X7[row] & 0x1F) << 1; // Center 5 bits in 8 cols
    awayRows[row] = (LETTER_A_5X7[row] & 0x1F) << 1;
    centerRows[row] = (FONT_5X7[2][row] & 0x1F) << 1;
  }

  for (uint8_t row = 0; row < 8; row++) {
    uint8_t reg = MAX7219_REG_DIGIT0 + row;
    max7219Send(reg, awayRows[row], reg, centerRows[row],
                reg, homeRows[row]);
  }

  Serial.println("[HW TEST] MAX7219 boot test: A=left, 2=center, H=right");
  delay(2000);
  setMax7219Scores(MAX7219_SCORE_BLANK, MAX7219_SCORE_BLANK);
}
