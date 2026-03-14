#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "globals.h"

// ============================================================================
// Voice Engine - Polyphonic voice allocation and lifecycle management
// ============================================================================

// Envelope state enum (shared with dsp.cpp)
enum VoiceEnvState { VOICE_ENV_IDLE, VOICE_ENV_ATTACK, VOICE_ENV_DECAY, VOICE_ENV_SUSTAIN, VOICE_ENV_RELEASE };

// Voice state arrays (extern for ISR access - lock-free by design)
// Marked volatile since they are shared between ISR and task contexts
extern volatile uint32_t voicePhase[POLYPHONY];
extern volatile uint32_t voiceStep[POLYPHONY];
extern volatile uint32_t voiceTargetStep[POLYPHONY];
extern volatile int32_t voiceEnvValue[POLYPHONY];
extern volatile uint8_t voiceKey[POLYPHONY];
extern volatile bool voiceActive[POLYPHONY];
extern volatile bool voiceRetrigger[POLYPHONY];
extern volatile uint8_t voiceEnvState[POLYPHONY];

// Initialize all voice arrays to default state
void voiceEngineInit(void);

// Update voice allocation and handle voice lifecycle (call from dspUpdateParams)
void voiceEngineUpdateParams(void);

// Get step size for MIDI note (pure function)
uint32_t voiceEngineGetStepSizeForMidiNote(int note);

// Get count of active voices
uint8_t voiceEngineGetActiveVoiceCount(void);
