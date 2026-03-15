#include "envelopes.h"
#include <Arduino.h>
#include "globals.h"
#include "voice_engine.h"

// ============================================================================
// Modulation envelope state (owned by envelopes module)
// ============================================================================
EnvState modEnvState = ENV_IDLE;
int32_t modEnvValue = 0;

void envelopeInit() {
    // Voice envelopes initialized in voice_engine
    modEnvState = ENV_IDLE;
    modEnvValue = 0;
}

void triggerEnvelopeRelease(uint8_t voiceIndex) {
    if (voiceIndex < POLYPHONY) {
        voices[voiceIndex].envState = ENV_RELEASE;
    }
}

uint8_t processEnvelope(uint8_t voiceIndex, const SynthParams& params) {
    if (voiceIndex >= POLYPHONY) return 0;

    // Access voice state through consolidated struct
    volatile VoiceState& voice = voices[voiceIndex];

    if (voice.retrigger) {
        voice.envValue = 1;
        voice.envState = ENV_ATTACK;
        voice.retrigger = false;
    }
    if (voice.envValue == 0 && voice.active) {
        voice.envValue = 1;
        voice.envState = ENV_ATTACK;
    }

    int32_t step, target;
    // Time scaling: parameter 0-127 maps to ~1ms to ~2s
    // Using quadratic scaling for more musical feel
    switch (voice.envState) {
        case ENV_ATTACK:
            // Attack: fast initial rise
            // Higher param = slower attack (more samples to reach peak)
            step = (255 << 8) / (1 + ((params.envAttack * params.envAttack) >> 4));
            voice.envValue += step;
            if (voice.envValue >= (255 << 8)) {
                voice.envValue = 255 << 8;
                voice.envState = ENV_DECAY;
            }
            break;
        case ENV_DECAY:
            // Decay: exponential-like fall to sustain level (piano-like)
            target = (params.envSustain * 255) << 1; // Sustain 0-127 -> 0-32385
            if (voice.envValue > target) {
                // Exponential decay: step proportional to current value
                step = voice.envValue / (8 + ((params.envDecay * params.envDecay) >> 5));
                if (step < 1) step = 1;
                voice.envValue -= step;
                if (voice.envValue <= target) {
                    voice.envValue = target;
                    voice.envState = ENV_SUSTAIN;
                }
            } else {
                voice.envState = ENV_SUSTAIN;
            }
            break;
        case ENV_SUSTAIN:
            // Hold at sustain level while key is held
            target = (params.envSustain * 255) << 1;
            voice.envValue = target;
            break;
        case ENV_RELEASE:
            // Release: exponential decay to zero (natural piano tail)
            step = voice.envValue / (8 + ((params.envRelease * params.envRelease) >> 5));
            if (step < 1) step = 1;
            voice.envValue -= step;
            if (voice.envValue <= 0) {
                voice.envValue = 0;
                voice.envState = ENV_IDLE;
            }
            break;
        default:
            voice.envValue = 0;
    }
    return voice.envValue >> 8;
}

int32_t processModEnvelope(const SynthParams& params) {
    int32_t step;
    switch (modEnvState) {
        case ENV_ATTACK:
            step = (255 << 8) / (1 + params.modEnvAttack * 2);
            modEnvValue += step;
            if (modEnvValue >= (255 << 8)) {
                modEnvValue = 255 << 8;
                modEnvState = ENV_DECAY;
            }
            break;
        case ENV_DECAY:
            step = (255 << 8) / (1 + params.modEnvDecay * 2);
            modEnvValue -= step;
            if (modEnvValue <= 0) {
                modEnvValue = 0;
                modEnvState = ENV_IDLE;
            }
            break;
        case ENV_RELEASE:
            modEnvValue -= (255 << 8) / (1 + params.modEnvDecay);
            if (modEnvValue <= 0) {
                modEnvValue = 0;
                modEnvState = ENV_IDLE;
            }
            break;
        default:
            modEnvValue = 0;
    }
    return ((modEnvValue >> 8) * params.modEnvAmount) / 64;
}
