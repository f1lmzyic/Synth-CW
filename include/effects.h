#pragma once
#include <stdint.h>

// Delay buffer size
#define DELAY_BUFFER_SIZE 8192

// Effect state structure (encapsulates all effect buffers and state)
struct EffectsState {
  int16_t delayBuffer[DELAY_BUFFER_SIZE];
  uint32_t delayWritePos;
};

// Initialize effect buffers and state
void effectsInit(EffectsState *state);

// Process delay effect with feedback and mix
// Returns: delayed signal mixed with input
int32_t processDelay(EffectsState *state, int32_t input, uint8_t delayTime,
                     uint8_t feedback, uint8_t mix);
