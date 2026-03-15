#include "ui.h"
#include "oscillators.h"

// Helper to generate a wave sample for UI visualization (-128 to 127)
// Delegates to the canonical getWaveSample in oscillators.h
int32_t uiGetWaveSample(WaveformType wave, uint8_t phaseMSB) {
    // Convert 8-bit phase to 32-bit for getWaveSample
    return getWaveSample(wave, static_cast<uint32_t>(phaseMSB) << 24);
}