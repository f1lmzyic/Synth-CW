#pragma once
#include <cstdint>

// ============================================================================
// Filter module - State Variable Filter (SVF)
// ============================================================================

// Filter state structure (ISR-safe, no dynamic allocation)
struct FilterState {
  int32_t f_lp = 0;
  int32_t f_bp = 0;
};

// Initialize filter states to zero
void filterInit(FilterState &state);

// Process State Variable Filter (LP/HP/BP/Notch)
// Returns filtered sample (-256 to 255 range)
int32_t processSVF(FilterState &state, int32_t input, uint8_t cutoff,
                   uint8_t resonance, uint8_t filterType);
