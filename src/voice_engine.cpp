#include "voice_engine.h"
#include <Arduino.h>
#include "constants.h"

// ============================================================================
// Voice allocation state (internal to voice engine)
// ============================================================================
static uint8_t voiceAllocIndex = 0;  // Round-robin allocator

// ============================================================================
// Voice state arrays - exported for ISR access (lock-free by design)
// Marked volatile since they are shared between ISR and task contexts
// ============================================================================
volatile uint32_t voicePhase[POLYPHONY] = {0};
volatile uint32_t voiceStep[POLYPHONY] = {0};
volatile uint32_t voiceTargetStep[POLYPHONY] = {0};
volatile int32_t voiceEnvValue[POLYPHONY] = {0};
volatile uint16_t voiceKey[POLYPHONY] = {0xFFFF};
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
    memset((void*)voiceKey, 0xFF, sizeof(voiceKey));  // 0xFFFF marks unused
    memset((void*)voiceActive, 0, sizeof(voiceActive));
    memset((void*)voiceRetrigger, 0, sizeof(voiceRetrigger));
    memset((void*)voiceEnvState, VOICE_ENV_IDLE, sizeof(voiceEnvState));
    voiceAllocIndex = 0;
}

// Helper: check if key is in pressed keys array
static bool isKeyPressed(uint16_t key) {
    for (uint8_t i = 0; i < sysState.pressedKeyCount; i++) {
        if (sysState.pressedKeys[i] == key) return true;
    }
    return false;
}

// Helper: find voice assigned to key, returns -1 if not found
static int8_t findVoiceForKey(uint16_t key) {
    for (int v = 0; v < POLYPHONY; v++) {
        if (voiceActive[v] && voiceKey[v] == key) return v;
    }
    return -1;
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
            uint16_t key = voiceKey[v];
            if (!isKeyPressed(key)) {
                voiceActive[v] = false;
                voiceKey[v] = 0xFFFF;
                voiceEnvState[v] = VOICE_ENV_RELEASE;
                voiceRetrigger[v] = false;
            }
        }
    }

    // Step 2: Allocate voices for newly pressed keys
    for (uint8_t i = 0; i < sysState.pressedKeyCount; i++) {
        uint16_t key = sysState.pressedKeys[i];

        // Skip if already has a voice
        if (findVoiceForKey(key) >= 0) continue;

        // Find free voice
        int8_t freeVoice = -1;
        for (int v = 0; v < POLYPHONY; v++) {
            if (!voiceActive[v] && voiceEnvState[v] == VOICE_ENV_IDLE) {
                freeVoice = v;
                break;
            }
        }

        // Voice stealing (round-robin) if no free voice
        if (freeVoice == -1) {
            freeVoice = voiceAllocIndex;
            voiceAllocIndex = (voiceAllocIndex + 1) % POLYPHONY;
            voiceEnvState[freeVoice] = VOICE_ENV_RELEASE;
        }

        // Allocate voice to key
        voiceKey[freeVoice] = key;
        voiceActive[freeVoice] = true;
        voiceRetrigger[freeVoice] = true;
        voiceEnvValue[freeVoice] = 0;
        voiceEnvState[freeVoice] = VOICE_ENV_ATTACK;

        // Calculate step size
        uint16_t keyboardId = key / KEYS_PER_KEYBOARD;
        uint8_t keyInKeyboard = key % KEYS_PER_KEYBOARD;
        int midiNote = (4 + keyboardId) * 12 + keyInKeyboard;
        voiceTargetStep[freeVoice] = voiceEngineGetStepSizeForMidiNote(midiNote);

        if (sysState.params.glideTime == 0) {
            voiceStep[freeVoice] = voiceTargetStep[freeVoice];
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
