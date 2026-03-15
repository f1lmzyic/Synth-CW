#include "filters.h"
#include "constants.h"

// ============================================================================
// Filter module - State Variable, Moog Ladder, MS-20 filter models
// All functions are pure calculations with state passed by reference
// ============================================================================

void filterInit(FilterState& state) {
    state.f_lp = 0;
    state.f_bp = 0;
    state.moog_s[0] = state.moog_s[1] = state.moog_s[2] = state.moog_s[3] = 0;
    state.ms20_s[0] = state.ms20_s[1] = 0;
}

int32_t applyFilterDrive(int32_t input, uint8_t driveAmount) {
    if (driveAmount == 0) return input;
    
    // Apply gain based on drive amount
    int32_t gain = 1 + (driveAmount >> 4);
    int32_t driven = input * gain;
    
    // Soft clipping (asymmetric saturation)
    if (driven > 255) {
        driven = 255 + ((driven - 255) >> 2);
    }
    if (driven < -256) {
        driven = -256 - ((-256 - driven) >> 2);
    }
    
    return driven;
}

int32_t processSVF(FilterState& state, int32_t input, uint8_t cutoff, uint8_t resonance, uint8_t filterType) {
    // State Variable Filter with LP/HP/BP/Notch outputs
    // Resonance: higher value = less feedback = more resonance
    int32_t q = 255 - (resonance * 2);
    if (q < FILTER_Q_MIN) q = FILTER_Q_MIN;  // Clamp feedback factor for stability
    
    // SVF core: integrate BP to get LP, differentiate LP to get BP, subtract for HP
    state.f_lp += ((cutoff * state.f_bp) >> 8);
    int32_t f_hp = input - state.f_lp - ((q * state.f_bp) >> 8);
    state.f_bp += ((cutoff * f_hp) >> 8);
    
    // Select output based on filter type
    switch (filterType) {
        case 0: return state.f_lp;                  // Lowpass
        case 1: return f_hp;                        // Highpass
        case 2: return state.f_bp;                  // Bandpass
        case 3: return state.f_lp + f_hp;           // Notch (LP + HP)
        default: return state.f_lp;
    }
}

int32_t processMoogFilter(FilterState& state, int32_t input, uint8_t cutoff, uint8_t resonance, uint8_t filterType) {
    // 4-pole Moog Ladder Filter emulation
    // Classic warm analog filter sound with resonance feedback
    
    // Resonance feedback from last stage
    int32_t q = resonance * 2;  // Scale to 0-254
    int32_t fb = (state.moog_s[3] * q) >> 8;
    int32_t filteredInput = input - fb;
    
    // Clamp input to prevent instability
    if (filteredInput > 255) filteredInput = 255;
    if (filteredInput < -256) filteredInput = -256;
    
    // 4 cascaded one-pole lowpass filters (ladder structure)
    state.moog_s[0] += ((filteredInput - state.moog_s[0]) * cutoff) >> 8;
    state.moog_s[1] += ((state.moog_s[0] - state.moog_s[1]) * cutoff) >> 8;
    state.moog_s[2] += ((state.moog_s[1] - state.moog_s[2]) * cutoff) >> 8;
    state.moog_s[3] += ((state.moog_s[2] - state.moog_s[3]) * cutoff) >> 8;
    
    // Select output based on filter type (approximations for non-LP types)
    switch (filterType) {
        case 0: return state.moog_s[3];             // Lowpass (4-pole)
        case 1: return input - state.moog_s[3];     // Highpass approximation
        case 2: return state.moog_s[2] - state.moog_s[3];  // Bandpass approximation
        case 3: return input - state.moog_s[2];     // Notch approximation
        default: return state.moog_s[3];
    }
}

int32_t processMS20Filter(FilterState& state, int32_t input, uint8_t cutoff, uint8_t resonance, uint8_t filterType) {
    // MS-20 Style aggressive filter
    // Known for harsh, resonant character with asymmetric clipping
    
    // Resonance feedback
    int32_t q = resonance;
    int32_t fb = (state.ms20_s[1] * q) >> 7;
    int32_t filteredInput = input - fb;
    
    // Asymmetric clipping (characteristic MS-20 saturation)
    if (filteredInput > 180) filteredInput = 180;
    if (filteredInput < -200) filteredInput = -200;
    
    // 2-pole state variable structure
    state.ms20_s[0] += ((filteredInput - state.ms20_s[0]) * cutoff) >> 8;
    state.ms20_s[1] += ((state.ms20_s[0] - state.ms20_s[1]) * cutoff) >> 8;
    
    // Select output based on filter type
    switch (filterType) {
        case 0: return state.ms20_s[1];             // Lowpass
        case 1: return filteredInput - state.ms20_s[0];  // Highpass
        case 2: return state.ms20_s[0] - state.ms20_s[1];  // Bandpass
        case 3: return state.ms20_s[1] + (filteredInput - state.ms20_s[0]);  // Notch
        default: return state.ms20_s[1];
    }
}
