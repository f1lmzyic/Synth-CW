#pragma once
#include <stdint.h>

// Buffer sizes
#define DELAY_BUFFER_SIZE 8192
#define CHORUS_BUFFER_SIZE 2048

// Effect state structure (encapsulates all effect buffers and state)
struct EffectsState {
  int16_t delayBuffer[DELAY_BUFFER_SIZE];
  int16_t chorusBuffer[CHORUS_BUFFER_SIZE];
  uint32_t delayWritePos;
  uint32_t chorusWritePos;
  uint32_t chorusLfoPhase;
  uint8_t decimatorCounter;
  int32_t lastDecimatorVal;
};

// Initialize effect buffers and state
void effectsInit(EffectsState *state);

// Process delay effect with feedback and mix
// Returns: delayed signal mixed with input
int32_t processDelay(EffectsState *state, int32_t input, uint8_t delayTime,
                     uint8_t feedback, uint8_t mix);

// Process chorus effect with LFO modulation
// Returns: chorused signal mixed with input
int32_t processChorus(EffectsState *state, int32_t input, uint8_t rate,
                      uint8_t depth, uint8_t mix);

// Process bitcrusher effect (bit depth reduction)
// Returns: bit-reduced signal
int32_t processBitcrusher(int32_t input, uint8_t depth);

// Process decimator effect (sample rate reduction)
// Returns: decimated signal
int32_t processDecimator(EffectsState *state, int32_t input, uint8_t rate);
