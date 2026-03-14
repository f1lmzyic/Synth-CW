#pragma once
#include <stdint.h>
#include "globals.h"

// ============================================================================
// LFO & Modulation Module
// LFO generation, sample & hold, and noise generation
// ============================================================================

// LFO state structure
typedef struct {
    uint32_t lfoPhase;        // Main LFO phase accumulator
    uint32_t chorusLfoPhase;  // Chorus LFO phase (separate rate)
    int32_t shValue;          // Sample & hold value
    uint32_t lastLfoPhase;    // For detecting LFO cycle boundaries
    uint32_t lfsrState;       // LFSR state for noise generation
} LfoState;

// Initialize LFO state
void lfoInit(LfoState* state);

// Process LFO - generate triangle wave (-128 to +127)
// Returns LFO value and updates state
int32_t processLFO(LfoState* state, uint8_t lfoRate);

// Generate white noise from LFSR (-128 to +127)
// Updates LFSR state and returns noise value
int32_t generateNoise(LfoState* state);

// Process Sample & Hold - sample noise at LFO cycle start
// Returns current S&H value
int32_t processSampleHold(LfoState* state, int32_t noiseVal);

// Process chorus LFO - separate LFO for chorus effect
// Returns chorus LFO value (uses sine wave)
int32_t processChorusLFO(LfoState* state, uint8_t chorusRate);
