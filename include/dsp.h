#pragma once
#include <Arduino.h>
#include "globals.h"

// Initialize DSP module
void dspInit();

// The main Audio ISR
void sampleISR();

// Update local DSP parameters from sysState (called periodically by a FreeRTOS task or from main loop)
void dspUpdateParams();
