#include "envelopes.h"
#include <Arduino.h>
#include "globals.h"
#include "voice_engine.h"

// Note: Voice state arrays (voiceKey, voiceActive, voiceRetrigger, voiceEnvState, voiceEnvValue)
//       are defined in voice_engine.cpp to avoid duplicate definitions

// Envelope state variables (owned by envelopes module)
EnvState modEnvState = ENV_IDLE;
int32_t modEnvValue = 0;

void envelopeInit() {
    // Voice envelopes initialized in voice_engine
    modEnvState = ENV_IDLE;
    modEnvValue = 0;
}

void triggerEnvelopeRelease(uint8_t voiceIndex) {
    if (voiceIndex < POLYPHONY)
        voiceEnvState[voiceIndex] = ENV_RELEASE;
}

uint8_t processEnvelope(uint8_t voiceIndex, const SynthParams& params) {
    if (voiceIndex >= POLYPHONY) return 0;
    
    if (voiceRetrigger[voiceIndex]) {
        voiceEnvValue[voiceIndex] = 1;
        voiceEnvState[voiceIndex] = ENV_ATTACK;
        voiceRetrigger[voiceIndex] = false;
    }
    if (voiceEnvValue[voiceIndex] == 0 && voiceActive[voiceIndex]) {
        voiceEnvValue[voiceIndex] = 1;
        voiceEnvState[voiceIndex] = ENV_ATTACK;
    }
    
    int32_t step, target;
    switch (voiceEnvState[voiceIndex]) {
        case ENV_ATTACK:
            step = (255 << 8) / (1 + params.envAttack * 2);
            voiceEnvValue[voiceIndex] += step;
            if (voiceEnvValue[voiceIndex] >= (255 << 8)) {
                voiceEnvValue[voiceIndex] = 255 << 8;
                voiceEnvState[voiceIndex] = ENV_SUSTAIN;
                target = params.envSustain << 9;
                step = (255 << 8) / (1 + params.envDecay * 2);
                voiceEnvValue[voiceIndex] -= step;
                if (voiceEnvValue[voiceIndex] < target)
                    voiceEnvValue[voiceIndex] = target;
            }
            break;
        case ENV_DECAY:
        case ENV_SUSTAIN:
            target = params.envSustain << 9;
            if (voiceEnvValue[voiceIndex] > target) {
                step = (255 << 8) / (1 + params.envDecay * 2);
                voiceEnvValue[voiceIndex] -= step;
                if (voiceEnvValue[voiceIndex] < target)
                    voiceEnvValue[voiceIndex] = target;
            }
            break;
        case ENV_RELEASE:
            step = (255 << 8) / (1 + params.envRelease * 2);
            voiceEnvValue[voiceIndex] -= step;
            if (voiceEnvValue[voiceIndex] <= 0) {
                voiceEnvValue[voiceIndex] = 0;
                voiceEnvState[voiceIndex] = ENV_IDLE;
            }
            break;
        default:
            voiceEnvValue[voiceIndex] = 0;
    }
    return voiceEnvValue[voiceIndex] >> 8;
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
