#pragma once
#include <Arduino.h>
#include <bitset>
#include "constants.h"
#include "globals.h"

// Rotary encoder knob class with quadrature decoding
class Knob {
public:
    Knob(uint8_t id) : m_id(id), m_prevState(0), m_lastDirection(0) {}

    // returns direction
    int8_t update(uint8_t currentState) {
        if (currentState == m_prevState) {
            return 0;
        }

        int8_t direction = 0;
        uint8_t transition = (m_prevState << 2) | currentState;

        switch (transition) {
            case 0b0100: case 0b1011: direction = 1; break;
            case 0b0001: case 0b1110: direction = -1;  break;
            // Handle missed transitions by continuing last direction
            case 0b0011: case 0b1100:
            case 0b0101: case 0b1010:
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

    uint8_t getId() const { return m_id; }

private:
    uint8_t m_id;
    uint8_t m_prevState;
    int8_t m_lastDirection;
};

void hwInit();
void setOutMuxBit(const uint8_t bitIdx, const bool value);
void setRow(uint8_t rowIdx, bool outVal = true);
std::bitset<4> readCols();

// Task handles
extern TaskHandle_t scanKeysHandle;

// Hardware tasks
void scanKeysTask(void * pvParameters);
