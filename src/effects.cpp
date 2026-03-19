#include "effects.h"
#include "globals.h"
#include <Arduino.h>

// Initialize effect buffers and state
void effectsInit(EffectsState *state) {
  memset(state->delayBuffer, 0, sizeof(state->delayBuffer));
  state->delayWritePos = 0;
}

// Process delay effect with feedback and mix
int32_t processDelay(EffectsState *state, int32_t input, uint8_t delayTime,
                     uint8_t feedback, uint8_t mix) {
  if (delayTime == 0)
    return input;

  // Use >>7 instead of /128
  uint32_t delayLen = ((uint32_t)delayTime * DELAY_BUFFER_SIZE) >> 7;
  if (delayLen < 100)
    delayLen = 100;

  uint32_t readPos =
      (state->delayWritePos + DELAY_BUFFER_SIZE - delayLen) % DELAY_BUFFER_SIZE;
  int32_t delayOut = state->delayBuffer[readPos];

  // Write with feedback
  int32_t fb = (delayOut * feedback) >> 7;
  state->delayBuffer[state->delayWritePos] = input + fb;
  state->delayWritePos = (state->delayWritePos + 1) % DELAY_BUFFER_SIZE;

  // Mix wet signal
  int32_t wet = (delayOut * mix) >> 7;
  return input + wet;
}
