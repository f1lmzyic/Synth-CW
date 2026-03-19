#pragma once
#include "globals.h"
#include <stdint.h>

// ============================================================================
// LFO & Modulation Module
// LFO generation and noise generation
// ============================================================================

// LFO state structure
typedef struct {
  uint32_t lfoPhase;       // Main LFO phase accumulator
  uint32_t lfsrState;      // LFSR state for noise generation
} LfoState;

// Initialize LFO state
void lfoInit(LfoState *state);

// Process LFO - generate triangle wave (-128 to +127)
// Returns LFO value and updates state
int32_t processLFO(LfoState *state, uint8_t lfoRate);

// Generate white noise from LFSR (-128 to +127)
// Updates LFSR state and returns noise value
int32_t generateNoise(LfoState *state);
