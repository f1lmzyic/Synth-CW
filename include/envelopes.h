#pragma once
#include "globals.h"
#include <Arduino.h>

// ============================================================================
// Envelope state machine - ADSR stages
// ============================================================================
enum EnvState { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

// ============================================================================
// Per-voice envelope state (declared in voice_engine.h, accessed here)
// Note: voiceKey, voiceActive, voiceRetrigger, voiceEnvState, voiceEnvValue
//       are all declared in voice_engine.h to avoid conflicts
// ============================================================================

// ============================================================================
// Public interface
// ============================================================================

// Initialize all envelope states to idle
void envelopeInit();

// Process ADSR envelope for a single voice
// Returns envelope value (0-255)
uint8_t processEnvelope(uint8_t voiceIndex, const SynthParams &params);

// Trigger release phase for a voice (called when key is released)
void triggerEnvelopeRelease(uint8_t voiceIndex);
