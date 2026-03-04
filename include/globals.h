#pragma once
#include <Arduino.h>
#include <bitset>
#include <STM32FreeRTOS.h>

// Waveform definitions
enum WaveformType {
    WAVEFORM_SAWTOOTH = 0,
    WAVEFORM_SQUARE,
    WAVEFORM_TRIANGLE,
    WAVEFORM_SINE,
    WAVEFORM_OFF,
    WAVEFORM_COUNT
};

extern const char* waveformNames[];

// UI Menu State
enum MenuPage {
    PAGE_OSC = 0,
    PAGE_OSC_EXT,
    PAGE_FLT,
    PAGE_FLT_MODEL,
    PAGE_ENV,
    PAGE_MOD,
    PAGE_MOD_ENV,
    PAGE_MOD_EXT,
    PAGE_FX,
    PAGE_FX_EXT,
    PAGE_PATCH, // Patch management page
    PAGE_COUNT
};

struct SynthParams {
    // OSC 1
    uint8_t osc1WaveMorph; // 0-255
    
    // OSC 2
    WaveformType osc2Wave;
    int8_t osc2Detune; // -50 to +50
    int8_t osc2Octave; // -2 to +2
    uint8_t mixOsc2;   // 0 to 100 (Osc1 is 100-mixOsc2)
    
    // OSC EXT
    uint8_t subOscMix;  // 0 to 100
    uint8_t noiseMix;   // 0 to 100
    uint8_t ringModMix; // 0 to 100
    
    // Filter
    uint8_t filterCutoff; // 0 to 127
    uint8_t filterRes;    // 0 to 127
    int8_t filterEnvDepth; // -64 to +64
    uint8_t filterType;   // 0=LP, 1=HP, 2=BP, 3=Notch
    uint8_t filterModel;  // 0=Standard SVF, 1=Moog Ladder (Drive), 2=MS-20 (Aggressive)
    uint8_t filterDrive;  // 0-127
    uint8_t wavefold;     // 0-127
    
    // Sync
    bool oscSync;
    
    // Envelope
    uint8_t envAttack;
    uint8_t envDecay;
    uint8_t envSustain;
    uint8_t envRelease;
    
    // Mod Envelope
    uint8_t modEnvAttack;
    uint8_t modEnvDecay;
    int8_t modEnvAmount; // -64 to +64
    uint8_t modEnvTarget; // 0=Pitch, 1=Filter, 2=Osc2Pitch
    
    // LFO & S&H
    uint8_t lfoRate;
    uint8_t lfoDepth;
    uint8_t lfoTarget; // 0=Pitch, 1=Filter, 2=PWM
    uint8_t shDepth;   // 0-127
    uint8_t shTarget;  // 0=Pitch, 1=Filter
    
    // Misc
    uint8_t glideTime;
    uint8_t delayTime;
    uint8_t delayFeedback;
    uint8_t delayMix;
    uint8_t chorusRate;
    uint8_t chorusDepth;
    uint8_t chorusMix;
    uint8_t bitcrushDepth; // 0-7
    uint8_t decimatorRate; // 0-127
    uint8_t masterVol;
};

struct SystemState {
    std::bitset<32> inputs;
    SemaphoreHandle_t mutex;

    // Audio Params (shared)
    SynthParams params;

    // UI State
    bool menuMode;
    MenuPage activePage;

    // Keyboard state
    int pressedKey; // -1 if no key
    uint32_t targetStepSize; // Used for glide

    // Patch management
    uint8_t currentPatchSlot;    // Currently loaded patch (0-15)
    bool patchDirty;             // True if current patch has unsaved changes

    // View mode (0=Performance, 1=Scope, 2=Envelope)
    uint8_t viewMode;
};

// Extern declaration for the shared state
extern SystemState sysState;
