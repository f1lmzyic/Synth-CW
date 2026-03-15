#include "oscillators.h"

// ============================================================================
// Waveform Names for UI Display
// ============================================================================
const char* waveformNames[] = {
    "Saw",
    "Square",
    "Triangle",
    "Sine",
    "Off"
};

// ============================================================================
// Oscillator Mixing
// ============================================================================

int32_t mixOscillators(uint32_t osc1Step, uint32_t osc2Step, uint32_t phase,
                       const SynthParams& params, int32_t noiseVal) {
    // Calculate OSC2 phase with detuning
    uint32_t osc2Phase = phase + ((phase >> 12) * (osc2Step - osc1Step) >> 12);
    
    // Hard sync: reset OSC2 phase when OSC1 wraps
    uint32_t oldPhase = phase;
    uint32_t newPhase = phase + osc1Step;
    if (params.oscSync && newPhase < oldPhase) {
        osc2Phase = 0;
    }
    
    // OSC1 with wave morphing between adjacent waveforms
    uint8_t waveIndex1 = params.osc1WaveMorph >> 6;  // 0-3 (which waveform)
    uint8_t waveIndex2 = waveIndex1 + 1;
    if (waveIndex2 > 3) waveIndex2 = 3;
    uint8_t morphFrac = (params.osc1WaveMorph & 0x3F) << 2; // 0-255
    
    int32_t osc1a = getWaveSample((WaveformType)waveIndex1, newPhase);
    int32_t osc1b = getWaveSample((WaveformType)waveIndex2, newPhase);
    int32_t osc1Out = ((osc1a * (255 - morphFrac)) + (osc1b * morphFrac)) >> 8;
    
    // OSC2
    int32_t osc2Out = getWaveSample(params.osc2Wave, osc2Phase);
    
    // Sub oscillator (1 octave below OSC1 - divide frequency by 2)
    int32_t subOut = ((newPhase & 0x80000000) ? 127 : -128);
    
    // Ring modulation (OSC1 * OSC2)
    int32_t ringOut = (osc1Out * osc2Out) >> 7;
    
    // Mix OSC1 and OSC2 based on mix parameter
    int32_t mix = params.mixOsc2;
    int32_t voiceMix = ((osc1Out * (100 - mix)) + (osc2Out * mix)) / 100;
    
    // Add sub oscillator, noise, and ring mod
    voiceMix += (subOut * params.subOscMix) / 100;
    voiceMix += (noiseVal * params.noiseMix) / 100;
    voiceMix += (ringOut * params.ringModMix) / 100;
    
    // Soft clip the mix bus (gentle saturation)
    if (voiceMix > 127) {
        voiceMix = 127 + ((voiceMix - 127) >> 2);
    }
    if (voiceMix < -128) {
        voiceMix = -128 - ((-128 - voiceMix) >> 2);
    }
    
    return voiceMix;
}

// ============================================================================
// Wavefolder Effect
// ============================================================================

int32_t applyWavefolder(int32_t input, uint8_t wavefold) {
    if (wavefold == 0) {
        return input;
    }
    
    // Calculate folding threshold and gain
    int32_t threshold = 127 - (wavefold >> 1);
    if (threshold < 32) threshold = 32;
    
    int32_t gain = 1 + (wavefold >> 4);
    int32_t folded = input * gain;
    
    // Fold the waveform at thresholds
    while (folded > threshold || folded < -threshold) {
        if (folded > threshold) {
            folded = (threshold << 1) - folded;
        } else if (folded < -threshold) {
            folded = -(threshold << 1) - folded;
        }
    }
    
    return folded;
}
