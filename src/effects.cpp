#include "effects.h"
#include "globals.h"
#include "sine_lut.h"
#include <Arduino.h>

// Initialize effect buffers and state
void effectsInit(EffectsState *state) {
  memset(state->delayBuffer, 0, sizeof(state->delayBuffer));
  memset(state->chorusBuffer, 0, sizeof(state->chorusBuffer));
  state->delayWritePos = 0;
  state->chorusWritePos = 0;
  state->chorusLfoPhase = 0;
  state->decimatorCounter = 0;
  state->lastDecimatorVal = 0;
}

// Process delay effect with feedback and mix
int32_t processDelay(EffectsState *state, int32_t input, uint8_t delayTime,
                     uint8_t feedback, uint8_t mix) {
  if (delayTime == 0)
    return input;

  uint32_t delayLen = ((uint32_t)delayTime * DELAY_BUFFER_SIZE) / 128;
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

// Process chorus effect with LFO modulation
int32_t processChorus(EffectsState *state, int32_t input, uint8_t rate,
                      uint8_t depth, uint8_t mix) {
  if (depth == 0 || mix == 0) {
    state->chorusBuffer[state->chorusWritePos] = input;
    state->chorusWritePos = (state->chorusWritePos + 1) % CHORUS_BUFFER_SIZE;
    return input;
  }

  // Advance chorus LFO
  state->chorusLfoPhase += 2000 + (rate * 2000);
  int32_t lfo = sineLUT[(state->chorusLfoPhase >> 24) & 0xFF]; // -128 to 127

  // Calculate modulated delay time
  uint32_t baseDelay = 256;
  int32_t modulation = (lfo * depth) >> 3;
  int32_t delayAmt = baseDelay + modulation;

  if (delayAmt < 10)
    delayAmt = 10;
  if (delayAmt >= CHORUS_BUFFER_SIZE)
    delayAmt = CHORUS_BUFFER_SIZE - 1;

  // Read from delay line
  uint32_t readPos = (state->chorusWritePos + CHORUS_BUFFER_SIZE - delayAmt) %
                     CHORUS_BUFFER_SIZE;
  int32_t chorusOut = state->chorusBuffer[readPos];

  // Write input
  state->chorusBuffer[state->chorusWritePos] = input;
  state->chorusWritePos = (state->chorusWritePos + 1) % CHORUS_BUFFER_SIZE;

  // Mix wet signal
  int32_t wet = (chorusOut * mix) / 100;
  return input + (wet >> 1);
}

// Process bitcrusher effect (bit depth reduction)
int32_t processBitcrusher(int32_t input, uint8_t depth) {
  if (depth == 0)
    return input;

  uint8_t bits = 8 - depth;
  if (bits < 1)
    bits = 1;
  int32_t shift = 8 - bits;
  return (input >> shift) << shift;
}

// Process decimator effect (sample rate reduction)
int32_t processDecimator(EffectsState *state, int32_t input, uint8_t rate) {
  if (rate == 0)
    return input;

  state->decimatorCounter++;
  uint8_t holdTime = (rate >> 4) + 1;

  if (state->decimatorCounter >= holdTime) {
    state->decimatorCounter = 0;
    state->lastDecimatorVal = input;
  }
  return state->lastDecimatorVal;
}
