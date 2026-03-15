#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "globals.h"

// ============================================================================
// Voice Engine - Polyphonic voice allocation and lifecycle management
// ============================================================================

// Envelope state enum (shared with dsp.cpp)
enum VoiceEnvState { VOICE_ENV_IDLE, VOICE_ENV_ATTACK, VOICE_ENV_DECAY, VOICE_ENV_SUSTAIN, VOICE_ENV_RELEASE };

// ============================================================================
// Voice state container - consolidates all per-voice state
// Marked volatile since shared between ISR and task contexts
// ============================================================================
struct VoiceState {
    uint32_t phase;         // Current phase accumulator
    uint32_t step;          // Current step size (may be gliding)
    uint32_t targetStep;    // Target step size after glide
    int32_t envValue;       // Envelope value (fixed-point 8.8)
    uint16_t key;           // Assigned key (0xFFFF = unused)
    uint8_t envState;       // VoiceEnvState
    bool active;            // Voice is sounding
    bool retrigger;         // Retrigger envelope on next sample
};

// Global voice state array - accessible from ISR (lock-free by design)
extern volatile VoiceState voices[POLYPHONY];

// ============================================================================
// Public Interface
// ============================================================================

// Initialize all voices to default state
void voiceEngineInit(void);

// Update voice allocation and handle voice lifecycle (call from dspUpdateParams)
void voiceEngineUpdateParams(void);

// Get step size for MIDI note (pure function)
uint32_t voiceEngineGetStepSizeForMidiNote(int note);

// Get count of active voices
uint8_t voiceEngineGetActiveVoiceCount(void);
