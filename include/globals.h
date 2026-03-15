#pragma once
#include <Arduino.h>
#include <bitset>
#include <STM32FreeRTOS.h>

class MutexGuard {
public:
    explicit MutexGuard(SemaphoreHandle_t mutex)
        : m_mutex(mutex), m_locked(false) {
        if (m_mutex != nullptr) {
            m_locked = (xSemaphoreTake(m_mutex, portMAX_DELAY) == pdTRUE);
        }
    }

    MutexGuard(SemaphoreHandle_t mutex, TickType_t timeout)
        : m_mutex(mutex), m_locked(false) {
        if (m_mutex != nullptr) {
            m_locked = (xSemaphoreTake(m_mutex, timeout) == pdTRUE);
        }
    }

    ~MutexGuard() {
        if (m_locked && m_mutex != nullptr) {
            xSemaphoreGive(m_mutex);
        }
    }

    bool isLocked() const { return m_locked; }

    explicit operator bool() const { return m_locked; }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;
    MutexGuard(MutexGuard&&) = delete;
    MutexGuard& operator=(MutexGuard&&) = delete;

private:
    SemaphoreHandle_t m_mutex;
    bool m_locked;
};

// ============================================================================
// Polyphony configuration
// ============================================================================
#define POLYPHONY 8              // Maximum simultaneous voices
#define MAX_KEYBOARD_IDS 3       // Maximum number of keyboards
#define KEYS_PER_KEYBOARD 12    // Keys per keyboard module
#define MAX_TOTAL_KEYS (MAX_KEYBOARD_IDS * KEYS_PER_KEYBOARD) // 36 keys max

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
    uint8_t viewMode; // 0=Performance, 1=Scope, 2=Envelope, 3=Menu

    // Parameter highlight (shows which param was last adjusted)
    uint8_t highlightedKnob; // 0-3, which knob was last turned
    uint32_t highlightEndTime; // timestamp when highlight expires
    
    // ============================================================================
    // Polyphonic key state - supports multiple simultaneous key presses
    // ============================================================================
    // Legacy single key support (for backward compatibility)
    int pressedKey; // -1 if no key
    uint32_t targetStepSize; // Used for glide
    
    // Multi-key tracking (new polyphonic system)
    volatile uint8_t pressedKeys[MAX_TOTAL_KEYS];    // Which keys (0-35) are currently pressed
    volatile uint8_t numPressedKeys;                 // Count of currently pressed keys
    volatile uint8_t keyboardId;                     // This keyboard's ID (0, 1, 2)
    volatile bool isPolyphonic;                      // True if polyphonic mode is active
    
    // Voice allocation state
    volatile uint8_t voiceKey[POLYPHONY];            // Which key (0-35) each voice is playing
    volatile bool voiceActive[POLYPHONY];            // Is each voice currently playing

    // Patch management
    uint8_t currentPatchSlot;    // Currently loaded patch (0-15)
    bool patchDirty;             // True if current patch has unsaved changes
    
    uint8_t RX_Message[8];
    bool isSenderNode; // true=sender, false=receiver
    uint8_t currentOctave; // Default octave
    int lastHandshakePos;
};

// Extern declaration for the shared state
extern SystemState sysState;

extern QueueHandle_t msgInQ;
extern QueueHandle_t msgOutQ;
