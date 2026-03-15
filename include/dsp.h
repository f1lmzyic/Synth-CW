#pragma once
#include <Arduino.h>
#include "globals.h"

// Include all modular DSP headers
#include "voice_engine.h"
#include "oscillators.h"
#include "envelopes.h"
#include "filters.h"
#include "effects.h"
#include "lfo_modulation.h"

// ============================================================================
// DSP Core - Main orchestration module
// Public API: dspInit(), sampleISR(), dspUpdateParams()
// ============================================================================

// Initialize all DSP modules (voice engine, envelopes, filters, effects, LFO)
void dspInit();

// The main audio ISR - orchestrates modular DSP signal flow
void sampleISR();

// Update local DSP parameters from sysState (called from main loop)
void dspUpdateParams();
