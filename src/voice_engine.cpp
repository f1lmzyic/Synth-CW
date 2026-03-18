#include "voice_engine.h"
#include "envelopes.h"
#include "constants.h"
#include "globals.h"
#include <Arduino.h>

// Pitch bend value (0-255, 128 = no bend)
volatile uint8_t pitchBendValue = PITCH_BEND_CENTER;

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
// Public Interface Implementation
// ============================================================================

void voiceEngineInit(void) {
  // Initialize each voice to default state (safe during init before ISR starts)
  for (int v = 0; v < POLYPHONY; v++) {
    voices[v].phase = 0;
    voices[v].step = 0;
    voices[v].targetStep = 0;
    voices[v].baseStep = 0;
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
    if (voices[v].active && voices[v].key == key) {
      return v;
    }
  }
  return -1;
}

// ============================================================================
// Pitch Bend Implementation
// Uses cents for smooth incremental pitch bend
// ============================================================================

// Apply pitch bend to a step size using cents
// bendValue: 0-255, where 128 = no bend (0 cents)
// Returns step size multiplied by pitch bend ratio
uint32_t applyPitchBend(uint32_t stepSize, uint8_t bendValue) {
  if (bendValue == PITCH_BEND_CENTER) {
    return stepSize;
  }

  // Calculate cents offset from center
  // 128 = 0 cents, 0 = -128 cents, 255 = +127 cents
  // Scale to ±200 cents (max ±2 semitones)
  int16_t centsOffset;
  if (bendValue < PITCH_BEND_CENTER) {
    centsOffset = -((PITCH_BEND_CENTER - bendValue) * 200 / PITCH_BEND_CENTER);
  } else {
    centsOffset = ((bendValue - PITCH_BEND_CENTER) * 200 / (255 - PITCH_BEND_CENTER));
  }

  // Clamp to ±200 cents
  if (centsOffset < -200) centsOffset = -200;
  if (centsOffset > 200) centsOffset = 200;

  // Convert cents to multiplier using 2^(cents/1200)
  // Use linear approximation: ratio ≈ 1 + cents * ln(2) / 1200
  // ln(2)/1200 ≈ 0.00057735
  // In Q16.16: multiplier = 65536 + cents * 37.85
  // Using 38 for slight overcorrection which sounds better

  uint32_t multiplier = 0x10000 + (centsOffset * 38);

  // Apply to step size
  uint64_t result = (static_cast<uint64_t>(stepSize) * multiplier) >> 16;
  return static_cast<uint32_t>(result);
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

  // Step 2: Allocate voices for newly pressed keys (limit to POLYPHONY)
  uint8_t keyCount = sysState.pressedKeyCount;
  if (keyCount > MAX_PRESSED_KEYS) keyCount = MAX_PRESSED_KEYS;
  if (keyCount > POLYPHONY) keyCount = POLYPHONY;  // Only process up to POLYPHONY keys
  for (uint8_t i = 0; i < keyCount; i++) {
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

    // Calculate step size
    uint16_t keyboardId = key / KEYS_PER_KEYBOARD;
    uint8_t keyInKeyboard = key % KEYS_PER_KEYBOARD;
    int octaveRelative = (int)keyboardId - (int)sysState.mainKeyboardId;
    int midiNote =
        (4 + octaveRelative + sysState.octaveOffset) * 12 + keyInKeyboard;
    uint32_t newBaseStep = voiceEngineGetStepSizeForMidiNote(midiNote);
    uint32_t newTargetStep = applyPitchBend(newBaseStep, pitchBendValue);

    // Set all voice fields, active LAST
    voices[freeVoice].key = key;
    voices[freeVoice].baseStep = newBaseStep;
    voices[freeVoice].targetStep = newTargetStep;
    voices[freeVoice].step = newTargetStep;
    voices[freeVoice].retrigger = true;
    voices[freeVoice].envValue = 0;
    voices[freeVoice].envState = VOICE_ENV_ATTACK;
    voices[freeVoice].active = true;

    // Trigger modulation envelope when note is played
    triggerModEnvelope();
  }

  // Step 3: Reapply current pitch bend to all active voices so that moving
  // the bend wheel while holding notes actually changes their pitch.
  for (int v = 0; v < POLYPHONY; v++) {
    if (voices[v].active) {
      voices[v].targetStep = applyPitchBend(voices[v].baseStep, pitchBendValue);
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
