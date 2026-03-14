#include "voice_engine.h"
#include <Arduino.h>
#include "constants.h"

// ============================================================================
// Voice allocation state (internal to voice engine)
// ============================================================================
static uint8_t voiceAllocIndex = 0;  // Round-robin allocator
static uint8_t pianoKeyMap[MAX_TOTAL_KEYS] = {0xFF};  // Reverse lookup

// ============================================================================
// Voice state arrays - exported for ISR access (lock-free by design)
// Marked volatile since they are shared between ISR and task contexts
// ============================================================================
volatile uint32_t voicePhase[POLYPHONY] = {0};
volatile uint32_t voiceStep[POLYPHONY] = {0};
volatile uint32_t voiceTargetStep[POLYPHONY] = {0};
volatile int32_t voiceEnvValue[POLYPHONY] = {0};
volatile uint8_t voiceKey[POLYPHONY] = {0xFF};
volatile bool voiceActive[POLYPHONY] = {false};
volatile bool voiceRetrigger[POLYPHONY] = {false};
volatile uint8_t voiceEnvState[POLYPHONY] = {VOICE_ENV_IDLE};

// Note frequencies - calibrated for 22kHz sample rate
static const uint32_t baseStepSizes[] = {
    51076057, 54113183, 57330935, 60740010, 64351798, 68178355,
    72232452, 76527617, 81078186, 85899345, 91007186, 96418755
};

// ============================================================================
// Public Interface Implementation
// ============================================================================

void voiceEngineInit(void) {
    // Cast away volatile for memset (safe during init before ISR starts)
    memset((void*)voicePhase, 0, sizeof(voicePhase));
    memset((void*)voiceStep, 0, sizeof(voiceStep));
    memset((void*)voiceTargetStep, 0, sizeof(voiceTargetStep));
    memset((void*)voiceEnvValue, 0, sizeof(voiceEnvValue));
    memset((void*)voiceKey, 0xFF, sizeof(voiceKey));
    memset((void*)voiceActive, 0, sizeof(voiceActive));
    memset((void*)voiceRetrigger, 0, sizeof(voiceRetrigger));
    memset(pianoKeyMap, 0xFF, sizeof(pianoKeyMap));
    memset((void*)voiceEnvState, VOICE_ENV_IDLE, sizeof(voiceEnvState));
    voiceAllocIndex = 0;
}

uint32_t voiceEngineGetStepSizeForMidiNote(int note) {
    if (note < 0) return 0;

    int noteIndex = note % 12;
    int octave = (note / 12) - 5;

    uint32_t stepSize = baseStepSizes[noteIndex];

    if (octave > 0) {
        stepSize <<= octave;
    } else if (octave < 0) {
        stepSize >>= (-octave);
    }
    return stepSize;
}

void voiceEngineUpdateParams(void) {
    // Step 1: Release voices for keys no longer pressed
    for (int v = 0; v < POLYPHONY; v++) {
        if (voiceActive[v]) {
            uint8_t key = voiceKey[v];
            if (key < MAX_TOTAL_KEYS && !sysState.pressedKeys[key]) {
                voiceActive[v] = false;
                voiceKey[v] = 0xFF;
                voiceEnvState[v] = VOICE_ENV_RELEASE;
                voiceRetrigger[v] = false;
                if (pianoKeyMap[key] == v) {
                    pianoKeyMap[key] = 0xFF;
                }
            }
        }
    }

    // NOTE: Release envelope processing is handled in sampleISR() at audio rate
    // to ensure smooth envelope decay. Don't duplicate it here.

    // Step 2: Allocate voices for newly pressed keys
    for (int i = 0; i < MAX_TOTAL_KEYS; i++) {
        if (sysState.pressedKeys[i] && pianoKeyMap[i] == 0xFF) {
            // Find free voice
            int8_t freeVoice = -1;
            for (int v = 0; v < POLYPHONY; v++) {
                if (!voiceActive[v]) {
                    freeVoice = v;
                    break;
                }
            }

            // Voice stealing (round-robin)
            if (freeVoice == -1) {
                freeVoice = voiceAllocIndex;
                voiceAllocIndex = (voiceAllocIndex + 1) % POLYPHONY;

                // Release old voice
                uint8_t oldKey = voiceKey[freeVoice];
                if (oldKey < MAX_TOTAL_KEYS) {
                    pianoKeyMap[oldKey] = 0xFF;
                }
                voiceKey[freeVoice] = 0xFF;
                voiceActive[freeVoice] = false;
                voiceEnvState[freeVoice] = VOICE_ENV_RELEASE;
                voiceRetrigger[freeVoice] = false;
            }

            // Allocate voice to key
            voiceKey[freeVoice] = i;
            voiceActive[freeVoice] = true;
            voiceRetrigger[freeVoice] = true;
            voiceEnvValue[freeVoice] = 0;
            voiceEnvState[freeVoice] = VOICE_ENV_ATTACK;
            pianoKeyMap[i] = freeVoice;

            // Calculate step size
            uint8_t keyboardId = i / KEYS_PER_KEYBOARD;
            uint8_t keyInKeyboard = i % KEYS_PER_KEYBOARD;
            int midiNote = (5 - keyboardId) * 12 + keyInKeyboard;
            voiceTargetStep[freeVoice] = voiceEngineGetStepSizeForMidiNote(midiNote);

            if (sysState.params.glideTime == 0) {
                voiceStep[freeVoice] = voiceTargetStep[freeVoice];
            }
        }
    }
}

uint8_t voiceEngineGetActiveVoiceCount(void) {
    uint8_t count = 0;
    for (int v = 0; v < POLYPHONY; v++) {
        if (voiceActive[v]) count++;
    }
    return count;
}
