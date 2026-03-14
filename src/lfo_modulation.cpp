#include "lfo_modulation.h"
#include "dsp.h"
#include "sine_lut.h"

// ============================================================================
// LFO & Modulation Module Implementation
// Pure functions for LFO, noise, and sample & hold generation
// ============================================================================

void lfoInit(LfoState* state) {
    state->lfoPhase = 0;
    state->chorusLfoPhase = 0;
    state->shValue = 0;
    state->lastLfoPhase = 0;
    state->lfsrState = 0xACE1u;  // LFSR seed
}

int32_t processLFO(LfoState* state, uint8_t lfoRate) {
    // Calculate LFO increment based on rate parameter
    uint32_t lfoInc = 5000 + (lfoRate * 3000);
    state->lfoPhase += lfoInc;
    
    // Generate triangle wave (-128 to +127)
    uint8_t phaseMSB = state->lfoPhase >> 24;
    int32_t lfoVal;
    
    if (phaseMSB < 128) {
        lfoVal = (phaseMSB << 1) - 128;  // Rising edge
    } else {
        lfoVal = 383 - (phaseMSB << 1);   // Falling edge
    }
    
    return lfoVal;
}

int32_t generateNoise(LfoState* state) {
    // LFSR-based white noise generation
    // Taps at bits 0, 2, 3, 5 (polynomial: x^32 + x^5 + x^3 + x^2 + 1)
    uint32_t bit = ((state->lfsrState >> 0) ^ (state->lfsrState >> 2) ^ 
                    (state->lfsrState >> 3) ^ (state->lfsrState >> 5)) & 1;
    
    // Shift and insert new bit
    state->lfsrState = (state->lfsrState >> 1) | (bit << 31);
    
    // Convert to audio range (-128 to +127)
    int32_t noiseVal = (int32_t)(state->lfsrState >> 24) - 128;
    
    return noiseVal;
}

int32_t processSampleHold(LfoState* state, int32_t noiseVal) {
    // Sample noise at LFO cycle boundary (when phase wraps)
    if (state->lfoPhase < state->lastLfoPhase) {
        state->shValue = noiseVal;
    }
    state->lastLfoPhase = state->lfoPhase;
    
    return state->shValue;
}

int32_t processChorusLFO(LfoState* state, uint8_t chorusRate) {
    // Separate LFO for chorus effect (sine wave)
    state->chorusLfoPhase += 2000 + (chorusRate * 2000);
    
    // Lookup sine wave from table
    uint8_t phaseMSB = state->chorusLfoPhase >> 24;
    int32_t chorusLfo = sineLUT[phaseMSB];
    
    return chorusLfo;
}
