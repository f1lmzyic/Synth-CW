#pragma once
#include <Arduino.h>
#include "globals.h"      // WaveformType enum, SynthParams
#include "sine_lut.h"     // sineLUT lookup table

// ============================================================================
// Oscillator Module - Waveform Generation, PolyBLEP, and Mixing
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
// PolyBLEP - Band-limited waveform synthesis
// Polynomial Band-Limited Step function for aliasing reduction
// ============================================================================

/**
 * PolyBLEP correction function
 * @param t Normalized time (0-1)
 * @param dt Phase increment as fraction of sample rate
 * @return Correction value to subtract from naive waveform
 */
inline float polyBLEP(float t, float dt) {
    if (t < dt) {
        t = t / dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/**
 * Band-limited sawtooth using PolyBLEP
 * @param phase 32-bit phase accumulator
 * @param phaseInc Phase increment per sample
 * @return Band-limited sawtooth sample (-128 to +127)
 */
inline int32_t getBLSaw(uint32_t phase, uint32_t phaseInc) {
    float t = (phase >> 8) / (float)0x01000000;
    float dt = phaseInc / (float)0x100000000;
    
    float sample = (2.0f * t - 1.0f) * 127.0f;
    sample -= polyBLEP(t, dt) * 127.0f;
    
    return (int32_t)sample;
}

/**
 * Band-limited square using PolyBLEP
 * @param phase 32-bit phase accumulator
 * @param phaseInc Phase increment per sample
 * @return Band-limited square sample (-128 to +127)
 */
inline int32_t getBLSquare(uint32_t phase, uint32_t phaseInc) {
    float t = (phase >> 8) / (float)0x01000000;
    float dt = phaseInc / (float)0x100000000;
    
    float sample = (t < 0.5f) ? 127.0f : -128.0f;
    sample -= polyBLEP(t, dt) * 255.0f;
    sample += polyBLEP(fmodf(t + 0.5f, 1.0f), dt) * 255.0f;
    
    return (int32_t)sample;
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
