#pragma once
#include <Arduino.h>

// ============================================================================
// Mode configuration
// ============================================================================

// Choose one:
//   MODE_BIDIRECTIONAL : send + receive (default, loopback test friendly)
//   MODE_SENDER_ONLY   : send only (no decode/audio from CAN)
//   MODE_RECEIVER_ONLY : receive only (ignores local key TX)
#define MODE_BIDIRECTIONAL 0
#define MODE_SENDER_ONLY 1
#define MODE_RECEIVER_ONLY 2
#define NODE_MODE MODE_BIDIRECTIONAL

// CAN loopback (true for single-board testing, false for real two-board link)
#define CAN_LOOPBACK false

// Set to true on the board connected to the power source (the "main" board).
// Set to false on all satellite boards (left/right extension keyboards).
// The main board:
//   - produces audio output
//   - processes all key events (own keys + CAN messages from satellites)
// Satellite boards:
//   - send key press/release events over CAN to the main board
//   - their octave is determined by position relative to the main board
//     (each position to the left = -1 octave, each position to the right = +1 octave)
#define IS_MAIN_BOARD true

// Sampling rate
const uint32_t SAMPLE_RATE = 22000;

// Pin definitions
// Row select and enable
const int RA0_PIN = D3;
const int RA1_PIN = D6;
const int RA2_PIN = D12;
const int REN_PIN = A5;

// Matrix input and output
const int C0_PIN = A2;
const int C1_PIN = D9;
const int C2_PIN = A6;
const int C3_PIN = D1;
const int OUT_PIN = D11;

// Audio analogue out
const int OUTL_PIN = A4;
const int OUTR_PIN = A3;

// Joystick analogue in
const int JOYX_PIN = A0;
const int JOYY_PIN = A1;

// Joystick calibration values
const int16_t JOY_CENTER_X = 540;
const int16_t JOY_CENTER_Y = 500;
const int16_t JOY_THRESHOLD = 150;
const int16_t JOY_DOWN_THRESHOLD = 700;
const int16_t JOY_UP_THRESHOLD = 300;

// DSP constants
const int32_t GLIDE_STEP_MIN = 500; // Minimum glide step size
const int32_t FILTER_Q_MIN = 8;     // Minimum Q value for filter stability

// Output multiplexer bits
const int KNOB_MODE = 2;
const int DEN_BIT = 3;
const int DRST_BIT = 4;
const int HKOW_BIT = 5;
const int HKOE_BIT = 6;

// Multi-keyboard handshake timing (ms)
const uint32_t HANDSHAKE_STARTUP_DELAY =
    1000;                                   // Wait after boot before handshake
const uint32_t HANDSHAKE_SETTLE_TIME = 200; // Wait after connection change
