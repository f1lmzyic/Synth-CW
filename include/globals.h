#pragma once
#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <bitset>

// #define TEST_SCANKEYS     // Test scanKeysTask worst-case (12 key messages) - 143 us
// #define TEST_DISPLAY      // Test displayUpdateTask worst-case - 16674 us
// #define TEST_DECODE       // Test decodeTask worst-case - 11us
// #define TEST_CANTX        // Test CAN_TX_Task worst-case - 4 us
// #define TEST_SAMPLEISR    // Test sampleISR worst-case - 22 us

// Number of iterations for timing measurement
#define TEST_ITERATIONS 32

// Auto-enable these when any test is active
#if defined(TEST_SCANKEYS) || defined(TEST_DISPLAY) || defined(TEST_DECODE) || \
defined(TEST_CANTX) || defined(TEST_SAMPLEISR)
  #define DISABLE_THREADS
  #define DISABLE_ISRS
#endif

// Maximum number of keys that can be tracked simultaneously
// Supports up to 3 connected keyboards (3 * 12 = 36 keys)
#define MAX_PRESSED_KEYS 36

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

  MutexGuard(const MutexGuard &) = delete;
  MutexGuard &operator=(const MutexGuard &) = delete;
  MutexGuard(MutexGuard &&) = delete;
  MutexGuard &operator=(MutexGuard &&) = delete;

private:
  SemaphoreHandle_t m_mutex;
  bool m_locked;
};

// ============================================================================
// Polyphony configuration
// ============================================================================
#define POLYPHONY 4          // Maximum simultaneous voices
#define KEYS_PER_KEYBOARD 12 // Keys per keyboard module

// Waveform definitions
enum WaveformType {
  WAVEFORM_SAWTOOTH = 0,
  WAVEFORM_SQUARE,
  WAVEFORM_TRIANGLE,
  WAVEFORM_SINE,
  WAVEFORM_OFF,
  WAVEFORM_COUNT
};

extern const char *waveformNames[];

// UI Menu State
enum MenuPage {
  PAGE_PERFORMANCE = 0,
  PAGE_OSC,
  PAGE_OSC_EXT,
  PAGE_FLT,
  PAGE_ENV,
  PAGE_MOD,
  PAGE_FX,
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
  uint8_t filterCutoff;  // 0 to 127
  uint8_t filterRes;     // 0 to 127
  int8_t filterEnvDepth; // -64 to +64
  uint8_t filterType;    // 0=LP, 1=HP, 2=BP, 3=Notch
  uint8_t filterModel;   // 0=Standard SVF, 1=Moog Ladder (Drive), 2=MS-20
                         // (Aggressive)
  uint8_t filterDrive;   // 0-127
  uint8_t wavefold;      // 0-127

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
  int8_t modEnvAmount;  // -64 to +64
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
  MenuPage activePage;

  // Parameter highlight (shows which param was last adjusted)
  uint8_t highlightedKnob;   // 0-3, which knob was last turned
  uint32_t highlightEndTime; // timestamp when highlight expires

  // ============================================================================
  // Polyphonic key state - supports multiple simultaneous key presses
  // ============================================================================
  uint16_t
      pressedKeys[MAX_PRESSED_KEYS]; // Array of pressed keys (0xFFFF = unused)
  uint8_t pressedKeyCount;           // Number of currently pressed keys
  volatile uint8_t keyboardId;       // This keyboard's ID (0, 1, 2, ...)
  uint8_t mainKeyboardId;            // ID of the board playing audio (set via CAN)
  int8_t octaveOffset;               // Octave offset (-2 to +2) for this module

  uint8_t RX_Message[8];
  int16_t
      lastHandshakePos; // -1 = no handshake, else position from left neighbor

  // Keyboard detection
  volatile bool hasLeft;
  volatile bool hasRight;

  // Multi-keyboard connection state
  bool prevWestIn;
  bool prevEastIn;
  bool eastOut;
  uint32_t lastConnectionChangeTime;

  // Pitch bend for UI display (read-only, set by hw.cpp)
  int8_t displayPitchBend;
  bool pitchBendEnabled;

  // Joystick analog state (set by scanJoystickTask)
  int16_t joystickX;
  int16_t joystickY;
};

// Extern declaration for the shared state
extern SystemState sysState;

extern QueueHandle_t msgInQ;
extern QueueHandle_t msgOutQ;

// ============================================================================
// Pitch Bend - Joystick Y-axis control
// Thread-safe via atomic operations (shared between ISR and tasks)
// Value: 0 = -2 semitones, 128 = center (no bend), 255 = +2 semitones
// ============================================================================
#define PITCH_BEND_CENTER 128
#define PITCH_BEND_MAX 2   // Maximum bend in semitones (2 = ±2 semitones)
extern volatile uint8_t pitchBendValue;

uint32_t applyPitchBend(uint32_t stepSize, uint8_t bendValue);
