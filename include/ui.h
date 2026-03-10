#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "globals.h"

// External display driver object
extern U8G2_SSD1305_128X32_ADAFRUIT_F_HW_I2C *u8g2;

// Init Display
void uiInit();

// UI update task
void displayUpdateTask(void * pvParameters);

// Handle parameter changes from knob rotation
void uiHandleKnobRotation(uint8_t knobIndex, int8_t direction);

// Helper for UI to generate a wave sample (-128 to 127) for visualization
int32_t uiGetWaveSample(WaveformType wave, uint8_t phaseMSB);
