#pragma once
#include <Arduino.h>
#include <bitset>
#include "constants.h"
#include "globals.h"

void hwInit();
void setOutMuxBit(const uint8_t bitIdx, const bool value);
void setRow(uint8_t rowIdx, bool outVal = true);
std::bitset<4> readCols();

// Task handles
extern TaskHandle_t scanKeysHandle;

// Hardware tasks
void scanKeysTask(void * pvParameters);
