#pragma once
#include <Arduino.h>
#include "globals.h"      // WaveformType enum, SynthParams
#include "sine_lut.h"     // sineLUT lookup table

// ============================================================================
// Oscillator Module - Waveform Generation and Mixing
// ============================================================================

// Waveform names for UI display
extern const char* waveformNames[];

// ============================================================================
// Waveform Generation (inline for performance)
// ============================================================================

/**
 * Generate wave sample from phase (-128 to +127)
 * @param wave Waveform type (saw, square, triangle, sine)
 * @param phase 32-bit phase accumulator
 * @return Signed sample value
 */
inline int32_t getWaveSample(WaveformType wave, uint32_t phase) {
    uint8_t phaseMSB = phase >> 24;
    switch (wave) {
        case WAVEFORM_SAWTOOTH:
            return (int16_t)phaseMSB - 128;
        case WAVEFORM_SQUARE:
            return (phaseMSB >= 128) ? 127 : -128;
        case WAVEFORM_TRIANGLE:
            return (phaseMSB < 128) ? ((phaseMSB << 1) - 128) : (383 - (phaseMSB << 1));
        case WAVEFORM_SINE:
            return sineLUT[phaseMSB];
        default:
            return 0;
    }
}

// ============================================================================
// Oscillator Mixing and Effects
// ============================================================================

/**
 * Mix multiple oscillators with detuning and octave shift
 * @param osc1Step OSC1 phase increment
 * @param osc2Step OSC2 phase increment (includes detune/octave)
 * @param phase Voice phase accumulator
 * @param params Synth parameters
 * @return Mixed oscillator output (-128 to +127)
 */
int32_t mixOscillators(uint32_t osc1Step, uint32_t osc2Step, uint32_t phase, 
                       const SynthParams& params, int32_t noiseVal);

/**
 * Apply wavefolder distortion effect
 * @param input Input sample
 * @param wavefold Amount (0-127)
 * @return Folded output sample
 */
int32_t applyWavefolder(int32_t input, uint8_t wavefold);

/**
 * Interpolate between two waveforms (wave morphing)
 * @param waveA First waveform type
 * @param waveB Second waveform type
 * @param phase Phase accumulator
 * @param morphFrac Morph fraction (0-255, 0=waveA, 255=waveB)
 * @return Morphed sample
 */
inline int32_t morphWave(WaveformType waveA, WaveformType waveB, uint32_t phase, uint8_t morphFrac) {
    int32_t sampleA = getWaveSample(waveA, phase);
    int32_t sampleB = getWaveSample(waveB, phase);
    return ((sampleA * (255 - morphFrac)) + (sampleB * morphFrac)) >> 8;
}
