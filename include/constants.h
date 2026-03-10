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

// Output multiplexer bits
const int KNOB_MODE = 2;
const int DEN_BIT = 3;
const int DRST_BIT = 4;
const int HKOW_BIT = 5;
const int HKOE_BIT = 6;
