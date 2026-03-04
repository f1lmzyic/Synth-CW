#include "ui.h"
#include "sine_lut.h"

// Helper to generate a wave sample for UI visualization (-128 to 127)
int32_t uiGetWaveSample(WaveformType wave, uint8_t phaseMSB) {
    switch (wave) {
        case WAVEFORM_SAWTOOTH:
            return phaseMSB - 128;
        case WAVEFORM_SQUARE:
            return (phaseMSB >= 128) ? 127 : -128;
        case WAVEFORM_TRIANGLE:
            return (phaseMSB < 128) ? ((phaseMSB << 1) - 128) : (383 - (phaseMSB << 1));
        case WAVEFORM_SINE: {
            return sineLUT[phaseMSB];
        }
        default:
            return 0;
    }
}
