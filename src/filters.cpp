#include "filters.h"
#include "constants.h"

// Initialize filter states to zero
void filterInit(FilterState &state) {
  state.f_lp = 0;
  state.f_bp = 0;
}

// State Variable Filter with LP/HP/BP/Notch outputs
int32_t processSVF(FilterState &state, int32_t input, uint8_t cutoff,
                    uint8_t resonance, uint8_t filterType) {
  // Resonance: higher value = less feedback = more resonance
  int32_t q = 255 - (resonance * 2);
  if (q < FILTER_Q_MIN)
    q = FILTER_Q_MIN; // Clamp feedback factor for stability

  // SVF core: integrate BP to get LP, differentiate LP to get BP, subtract for HP
  state.f_lp += ((cutoff * state.f_bp) >> 8);
  int32_t f_hp = input - state.f_lp - ((q * state.f_bp) >> 8);
  state.f_bp += ((cutoff * f_hp) >> 8);

  // Select output based on filter type
  switch (filterType) {
    case 0:
      return state.f_lp; // Lowpass
    case 1:
      return f_hp; // Highpass
    case 2:
      return state.f_bp; // Bandpass
    case 3:
      return state.f_lp + f_hp; // Notch (LP + HP)
    default:
      return state.f_lp;
  }
}
