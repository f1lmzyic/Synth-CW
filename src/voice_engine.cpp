#include "voice_engine.h"
#include "constants.h"
#include <Arduino.h>

// ============================================================================
// Voice state - single array of structs (better cache locality)
// ============================================================================
volatile VoiceState voices[POLYPHONY];

// Round-robin allocator index (internal to voice engine)
static uint8_t voiceAllocIndex = 0;

// Note frequencies - calibrated for 22kHz sample rate
static const uint32_t baseStepSizes[] = {
    51076057, 54113183, 57330935, 60740010, 64351798, 68178355,
    72232452, 76527617, 81078186, 85899345, 91007186, 96418755};

// ============================================================================
// Public Interface Implementation
// ============================================================================

void voiceEngineInit(void) {
  // Initialize each voice to default state (safe during init before ISR starts)
  for (int v = 0; v < POLYPHONY; v++) {
    voices[v].phase = 0;
    voices[v].step = 0;
    voices[v].targetStep = 0;
    voices[v].envValue = 0;
    voices[v].key = 0xFFFF; // Marks unused
    voices[v].envState = VOICE_ENV_IDLE;
    voices[v].active = false;
    voices[v].retrigger = false;
  }
  voiceAllocIndex = 0;
}

// Helper: check if key is in pressed keys array
static bool isKeyPressed(uint16_t key) {
  for (uint8_t i = 0; i < sysState.pressedKeyCount; i++) {
    if (sysState.pressedKeys[i] == key)
      return true;
  }
  return false;
}

// Helper: find voice assigned to key, returns -1 if not found
static int8_t findVoiceForKey(uint16_t key) {
  for (int v = 0; v < POLYPHONY; v++) {
    if (voices[v].active && voices[v].key == key)
      return v;
  }
  return -1;
}

uint32_t voiceEngineGetStepSizeForMidiNote(int note) {
  if (note < 0)
    return 0;

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
    if (voices[v].active) {
      uint16_t key = voices[v].key;
      if (!isKeyPressed(key)) {
        voices[v].active = false;
        voices[v].key = 0xFFFF;
        voices[v].envState = VOICE_ENV_RELEASE;
        voices[v].retrigger = false;
      }
    }
  }

  // Step 2: Allocate voices for newly pressed keys
  for (uint8_t i = 0; i < sysState.pressedKeyCount; i++) {
    uint16_t key = sysState.pressedKeys[i];

    // Skip if already has a voice
    if (findVoiceForKey(key) >= 0)
      continue;

    // Find free voice
    int8_t freeVoice = -1;
    for (int v = 0; v < POLYPHONY; v++) {
      if (!voices[v].active && voices[v].envState == VOICE_ENV_IDLE) {
        freeVoice = v;
        break;
      }
    }

    // Voice stealing (round-robin) if no free voice
    if (freeVoice == -1) {
      freeVoice = voiceAllocIndex;
      voiceAllocIndex = (voiceAllocIndex + 1) % POLYPHONY;
      voices[freeVoice].envState = VOICE_ENV_RELEASE;
    }

    // Allocate voice to key
    voices[freeVoice].key = key;
    voices[freeVoice].active = true;
    voices[freeVoice].retrigger = true;
    voices[freeVoice].envValue = 0;
    voices[freeVoice].envState = VOICE_ENV_ATTACK;

    // Calculate step size (apply octave offset from UI)
    uint16_t keyboardId = key / KEYS_PER_KEYBOARD;
    uint8_t keyInKeyboard = key % KEYS_PER_KEYBOARD;
    int midiNote =
        (4 + keyboardId + sysState.octaveOffset) * 12 + keyInKeyboard;
    voices[freeVoice].targetStep = voiceEngineGetStepSizeForMidiNote(midiNote);

    if (sysState.params.glideTime == 0) {
      voices[freeVoice].step = voices[freeVoice].targetStep;
    }
  }
}

uint8_t voiceEngineGetActiveVoiceCount(void) {
  uint8_t count = 0;
  for (int v = 0; v < POLYPHONY; v++) {
    if (voices[v].active)
      count++;
  }
  return count;
}
