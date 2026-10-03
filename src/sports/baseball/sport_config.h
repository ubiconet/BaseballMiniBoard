#pragma once

#include <Arduino.h>

// Baseball MiniBoard hardware profile.

// Three cascaded MAX7219 8x8 matrices.
static const int MAX7219_DIN_PIN = 14;
static const int MAX7219_CLK_PIN = 8;
static const int MAX7219_CS_PIN = 16;

// Discrete count LED pin assignments.
static const int OUT_2_PIN = 1;
static const int OUT_1_PIN = 2;
static const int STRIKE_2_PIN = 3;
static const int STRIKE_1_PIN = 4;
static const int BALL_3_PIN = 5;
static const int BALL_2_PIN = 6;
static const int BALL_1_PIN = 7;

static const char* NETWORK_AP_SSID = "BASEBALL_MINIBOARD";
static const char* NETWORK_HOSTNAME = "baseball-miniboard";
