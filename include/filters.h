#pragma once
#include <cstdint>

// ============================================================================
// Filter module - State Variable, Moog Ladder, MS-20 filter models
// ============================================================================

// Filter state structure (ISR-safe, no dynamic allocation)
struct FilterState {
    // SVF state
    int32_t f_lp = 0;
    int32_t f_bp = 0;

    // Moog ladder state (4-pole)
    int32_t moog_s[4] = {0, 0, 0, 0};

    // MS-20 state (2-pole)
    int32_t ms20_s[2] = {0, 0};
};

// Initialize filter states to zero
void filterInit(FilterState& state);

// Process State Variable Filter (LP/HP/BP/Notch)
// Returns filtered sample (-256 to 255 range)
int32_t processSVF(FilterState& state, int32_t input, uint8_t cutoff, uint8_t resonance, uint8_t filterType);

// Process Moog Ladder Filter (4-pole lowpass with resonance)
// Returns filtered sample (-256 to 255 range)
int32_t processMoogFilter(FilterState& state, int32_t input, uint8_t cutoff, uint8_t resonance, uint8_t filterType);

// Process MS-20 Style Filter (aggressive 2-pole)
// Returns filtered sample (-256 to 255 range)
int32_t processMS20Filter(FilterState& state, int32_t input, uint8_t cutoff, uint8_t resonance, uint8_t filterType);

// Apply pre-filter drive/soft clipping
// Returns driven sample with soft clipping applied
int32_t applyFilterDrive(int32_t input, uint8_t driveAmount);
