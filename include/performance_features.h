#pragma once
#include <Arduino.h>
#include "globals.h"
#include "constants.h"

// Chord memory
void chordExpandHeldKeys(const uint16_t* heldKeys, uint8_t heldCount,
                         uint16_t* outKeys, uint8_t* outCount);

// Arpeggiator
void arpBuildOutputKeys(const uint16_t* inKeys, uint8_t inCount,
                        uint16_t* outKeys, uint8_t* outCount);
void arpResetState();

// Automation
void automationInit();
void automationStepTick();
void automationClear();
const char* automationMaskName(uint8_t mask);

// UI helpers
const char* arpModeName(uint8_t mode);
const char* chordTypeName(uint8_t type);