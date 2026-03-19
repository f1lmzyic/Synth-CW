#pragma once
#include "constants.h"
#include "globals.h"
#include <Arduino.h>
#include <bitset>

// Rotary encoder knob class with quadrature decoding
class Knob {
public:
  Knob() : m_prevState(0), m_lastDirection(0) {}

  // Returns direction: -1, 0, or 1
  int8_t update(uint8_t currentState) {
    if (currentState == m_prevState) {
      return 0;
    }

    int8_t direction = 0;
    uint8_t transition = (m_prevState << 2) | currentState;

    switch (transition) {
    case 0b0100:
    case 0b1011:
      direction = 1;
      break;
    case 0b0001:
    case 0b1110:
      direction = -1;
      break;
    // Handle missed transitions by continuing last direction
    case 0b0011:
    case 0b1100:
    case 0b0101:
    case 0b1010:
      direction = m_lastDirection;
      break;
    default:
      direction = 0;
      break;
    }

    if (direction != 0) {
      m_lastDirection = direction;
    }
    m_prevState = currentState;

    return direction;
  }

private:
  uint8_t m_prevState;
  int8_t m_lastDirection;
};

void hwInit();
void setOutMuxBit(const uint8_t bitIdx, const bool value);
void setRow(uint8_t rowIdx, bool outVal = true);
std::bitset<4> readCols();

// GPIO register access helpers (inline for zero overhead)
// Pin mappings for Nucleo-L432KC:
//   RA0=D3=PA11, RA1=D6=PB5, RA2=D12=PB9, REN=A5=PA5
//   C0=A2=PA2, C1=D9=PA7, C2=A6=PA6, C3=D1=PA9, OUT=D11=PB8

// Lower 16 bits: set pin high; Upper 16 bits: set pin low
inline void setRowFast(uint8_t rowIdx) {
  // Decode rowIdx bits to row select lines
  // Row 0 = 000, Row 1 = 001, Row 2 = 010, Row 3 = 011, Row 4 = 100, etc.
  
  // RA0 (PA11): bit 0 of rowIdx
  if (rowIdx & 0x01) {
    GPIOA->BSRR = (1U << 11);        // Set PA11 high
  } else {
    GPIOA->BSRR = (1U << (11 + 16)); // Reset PA11 low
  }
  
  // RA1 (PB5): bit 1 of rowIdx
  if ((rowIdx >> 1) & 0x01) {
    GPIOB->BSRR = (1U << 5);         // Set PB5 high
  } else {
    GPIOB->BSRR = (1U << (5 + 16));  // Reset PB5 low
  }
  
  // RA2 (PB9): bit 2 of rowIdx
  if ((rowIdx >> 2) & 0x01) {
    GPIOB->BSRR = (1U << 9);         // Set PB9 high
  } else {
    GPIOB->BSRR = (1U << (9 + 16));  // Reset PB9 low
  }
}

// column reading using single GPIO port register reads
// Reads C0, C1, C2, C3 from GPIOA IDR
// Returns bitmask: bit 0 = C0, bit 1 = C1, bit 2 = C2, bit 3 = C3
inline uint32_t readColsFast() {
  // Read all GPIOA pins at once
  uint32_t gpioaInput = GPIOA->IDR;
  
  // Extract column pins: C0=PA2, C1=PA7, C2=PA6, C3=PA9
  // Each pin extracted and placed into corresponding bit position
  uint32_t cols = 0;
  cols |= ((gpioaInput >> 2) & 0x01) << 0;  // C0 from PA2 -> bit 0
  cols |= ((gpioaInput >> 7) & 0x01) << 1;  // C1 from PA7 -> bit 1
  cols |= ((gpioaInput >> 6) & 0x01) << 2;  // C2 from PA6 -> bit 2
  cols |= ((gpioaInput >> 9) & 0x01) << 3;  // C3 from PA9 -> bit 3
  
  return cols;
}

// multiplexer enable/disable (REN_PIN = A5 = PA5, active low)
inline void enableMuxFast() {
  GPIOA->BSRR = (1U << (5 + 16));  // Reset PA5 low to enable mux
}

inline void disableMuxFast() {
  GPIOA->BSRR = (1U << 5);         // Set PA5 high to disable mux
}

// output latch bit set (OUT_PIN = D11 = PB8)
inline void setOutFast(bool value) {
  if (value) {
    GPIOB->BSRR = (1U << 8);        // Set PB8 high
  } else {
    GPIOB->BSRR = (1U << (8 + 16)); // Reset PB8 low
  }
}

// Task handles
extern TaskHandle_t scanKeysHandle;
extern TaskHandle_t scanKnobsHandle;
extern TaskHandle_t pitchBendHandle;
extern TaskHandle_t scanJoystickHandle;

// Hardware tasks
void scanKeysTask(void *pvParameters);
void scanKnobsTask(void *pvParameters);
[[noreturn]] void pitchBendTask(void *pvParameters);
void scanJoystickTask(void *pvParameters);

uint32_t getKeyMask();
