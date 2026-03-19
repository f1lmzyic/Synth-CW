#include "hw.h"
#include "constants.h"
#include "dsp.h" // For dspUpdateParams
#include "navigation.h"
#include "ui.h" // For uiHandleKnobRotation
#include "voice_engine.h" // For pitchBendValue
#include <atomic>

TaskHandle_t scanKeysHandle = NULL;
TaskHandle_t scanKnobsHandle = NULL;
TaskHandle_t pitchBendHandle = NULL;
TaskHandle_t scanJoystickHandle = NULL;

static std::atomic<uint32_t> atomicKeyMask{0};

// Joystick Y cache: written by scanKeysTask, read by pitchBendTask.
static volatile int16_t cachedJoyY = JOY_CENTER_Y;

// ============================================================================
// Multi-keyboard connection handling
// ============================================================================
static void handleConnectionChange(bool westIn, bool eastIn, uint32_t now) {
    // Reset handshake state to allow re-negotiation
    sysState.eastOut = true;
    // Only clear our left-neighbor position if the LEFT side physically
    // disconnected (westIn went true→false). When westIn goes false→true it
    // means the left board finished its handshake — we want to KEEP the {H,N}
    // CAN message we already received from it.
    if (!westIn && sysState.prevWestIn) {
        sysState.lastHandshakePos = -1;
    }
    sysState.lastConnectionChangeTime = now;

    // Clear pressed keys from disconnected keyboards
    MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
    if (lock) {
        if (!westIn && sysState.prevWestIn) {
            // Left neighbor disconnected - clear keys from left keyboards
            uint16_t maxKeyToRemove = sysState.keyboardId * KEYS_PER_KEYBOARD;
            uint8_t writeIdx = 0;
            for (uint8_t i = 0; i < sysState.pressedKeyCount; i++) {
                if (sysState.pressedKeys[i] >= maxKeyToRemove) {
                    sysState.pressedKeys[writeIdx++] = sysState.pressedKeys[i];
                }
            }
            sysState.pressedKeyCount = writeIdx;
        }
    }

    sysState.prevWestIn = westIn;
    sysState.prevEastIn = eastIn;
}

// ============================================================================
// Perform handshake to determine keyboard position
// ============================================================================
static void performHandshake(bool westIn, bool eastIn) {
    // Wait only if we have a physical west neighbor AND haven't yet received
    // their {H} CAN message (lastHandshakePos still -1).  Once we receive their
    // position we can proceed regardless of the westIn signal level.
    if (westIn && sysState.lastHandshakePos < 0)
        return;

    sysState.eastOut = false; // Signal right neighbor we're ready

    MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
    if (lock) {
        sysState.hasLeft = (sysState.lastHandshakePos >= 0);
        sysState.hasRight = eastIn;

        uint8_t prevKeyboardId = sysState.keyboardId;

        // Determine keyboard position in chain
        if (!sysState.hasLeft) {
            sysState.keyboardId = 0;
        } else {
            sysState.keyboardId = sysState.lastHandshakePos + 1;
        }

        // if the main board and our position shifted (e.g. board plugged to the left),
        // update our mainKeyboardId to match our new position so it remain the main board.
        if (sysState.mainKeyboardId == prevKeyboardId) {
            sysState.mainKeyboardId = sysState.keyboardId;
        }
    }

    // Broadcast position for right neighbor
    uint8_t pos = sysState.hasLeft ? sysState.lastHandshakePos + 1 : 0;
    uint8_t hMsg[8] = {'H', pos, 0, 0, 0, 0, 0, 0};
    xQueueSend(msgOutQ, hMsg, pdMS_TO_TICKS(50));

    // Main board immediately broadcasts its ID so satellites get their octave
    // display without waiting for the periodic {M} cycle.
    if (!sysState.hasLeft) {
        uint8_t mMsg[8] = {'M', pos, 0, 0, 0, 0, 0, 0};
        xQueueSend(msgOutQ, mMsg, pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// Update connection state (call every scan cycle)
// ============================================================================
static void updateConnectionState(bool westIn, bool eastIn) {
    uint32_t now = millis();

    // Detect connection change
    if ((westIn != sysState.prevWestIn) || (eastIn != sysState.prevEastIn)) {
        handleConnectionChange(westIn, eastIn, now);
        return;
    }

    // Handshake after startup or after connection change settles
    bool readyToHandshake =
            (now > HANDSHAKE_STARTUP_DELAY) &&
            (now - sysState.lastConnectionChangeTime > HANDSHAKE_SETTLE_TIME);
    if (readyToHandshake && sysState.eastOut) {
        performHandshake(westIn, eastIn);
    }
}

void hwInit() {
    pinMode(RA0_PIN, OUTPUT);
    pinMode(RA1_PIN, OUTPUT);
    pinMode(RA2_PIN, OUTPUT);
    pinMode(REN_PIN, OUTPUT);
    pinMode(OUT_PIN, OUTPUT);
    pinMode(OUTL_PIN, OUTPUT);
    pinMode(OUTR_PIN, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);

    pinMode(C0_PIN, INPUT);
    pinMode(C1_PIN, INPUT);
    pinMode(C2_PIN, INPUT);
    pinMode(C3_PIN, INPUT);
    pinMode(JOYX_PIN, INPUT);
    pinMode(JOYY_PIN, INPUT);
}

// Lock-free key state read
uint32_t getKeyMask() {
    return atomicKeyMask.load(std::memory_order_relaxed);
}

void setOutMuxBit(const uint8_t bitIdx, const bool value) {
    digitalWrite(REN_PIN, LOW);
    digitalWrite(RA0_PIN, bitIdx & 0x01);
    digitalWrite(RA1_PIN, bitIdx & 0x02);
    digitalWrite(RA2_PIN, bitIdx & 0x04);
    digitalWrite(OUT_PIN, value);
    digitalWrite(REN_PIN, HIGH);
    delayMicroseconds(2);
    digitalWrite(REN_PIN, LOW);
}

void setRow(uint8_t rowIdx, bool outVal) {
    digitalWrite(REN_PIN, LOW);
    digitalWrite(RA0_PIN, rowIdx & 0x01);
    digitalWrite(RA1_PIN, (rowIdx >> 1) & 0x01);
    digitalWrite(RA2_PIN, (rowIdx >> 2) & 0x01);
    digitalWrite(OUT_PIN, outVal);
    digitalWrite(REN_PIN, HIGH);
}

std::bitset<4> readCols() {
    return (digitalRead(C3_PIN) << 3) | (digitalRead(C2_PIN) << 2) | (digitalRead(C1_PIN) << 1) | digitalRead(C0_PIN);
}

void scanKeysTask(void *pvParameters) {
#ifndef TEST_SCANKEYS
    const TickType_t xFrequency = pdMS_TO_TICKS(20);
    TickType_t xLastWakeTime = xTaskGetTickCount();
#endif

    static Knob knobs[4];
    static bool prevJoyButton = false;

    bool requestPitchBendToggle = false;

    // Enable mux once for entire scan cycle
    enableMuxFast();
    setOutFast(true);

#ifdef TEST_SCANKEYS
    // Test mode: run once without blocking
#else
    while (true) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
#endif

    std::bitset<32> localInputs;

    for (int row = 0; row < 5; row++) {
      setRow(row, true);
      delayMicroseconds(3);
      std::bitset<4> cols = readCols();
      for (uint8_t col = 0; col < 4; col++) localInputs[row * 4 + col] = cols[col];

        // Decode knobs from rows 3 and 4
        if (row == 3) {
            int8_t dir3 = knobs[3].update(cols[0] << 1 | cols[1]);
            if (dir3 != 0)
                uiHandleKnobRotation(3, dir3);
            int8_t dir2 = knobs[2].update(cols[2] << 1 | cols[3]);
            if (dir2 != 0)
                uiHandleKnobRotation(2, dir2);
        } else if (row == 4) {
            int8_t dir1 = knobs[1].update(cols[0] << 1 | cols[1]);
            int8_t dir0 = knobs[0].update(cols[2] << 1 | cols[3]);
            if (dir1 != 0)
                uiHandleKnobRotation(1, dir1);
            if (dir0 != 0)
                uiHandleKnobRotation(0, dir0);
        }
    }

    // Read joystick button (row 5, col 2) and West input (row 5, col 3)
    setRow(5, true);
    delayMicroseconds(3);
    std::bitset<4> col5 = readCols();
    localInputs[22] = col5[2]; // Joystick S button
    localInputs[23] = col5[3]; // West Input
    const bool westIn = !col5[3];
    const bool joyButton = !col5[2];

    // Toggle pitch bend on button press - wait for batched mutex block
    if (joyButton && !prevJoyButton) {
        requestPitchBendToggle = true;
    }
    prevJoyButton = joyButton;

    // Read East input (row 6, col 3)
    setRow(6, sysState.eastOut);
    delayMicroseconds(3);
    std::bitset<4> cols6 = readCols();
    bool eastIn = !cols6[3]; // East Input

    // Hot-plug connection detection and handshake
    updateConnectionState(westIn, eastIn);

    // ============================================================================
    // Multi-key detection - detect ALL pressed keys (0-11)
    // ============================================================================
    // Track which keys are currently pressed (bitmask: bit i = key i state)
    static uint32_t currentKeyMask = 0;
    static uint32_t previousKeyMask = 0;

    // Build current key mask from localInputs (bit i = 1 if key i pressed)
    currentKeyMask = 0;
    for (int i = 0; i < KEYS_PER_KEYBOARD; i++) {
        // localInputs[i] == 0 means key is pressed (active low)
        if (!localInputs[i]) {
            currentKeyMask |= (1U << i);
        }
    }

    // All boards that have completed their handshake (eastOut=false) periodically
    // re-broadcast {H, keyboardId} so any newly hot-plugged right-side board
    // can receive the position info it needs to complete its own handshake.
    // Uses a shared counter; right-side boards use max-wins update so the
    // largest (nearest-left-neighbour) position always wins.
    static uint8_t hBroadcastCounter = 0;
    if (!sysState.eastOut && (sysState.hasLeft || sysState.hasRight)) {
        if (++hBroadcastCounter >= 10) {
            // every 200 ms
            hBroadcastCounter = 0;
            uint8_t hMsg[8] = {'H', sysState.keyboardId, 0, 0, 0, 0, 0, 0};
            xQueueSend(msgOutQ, hMsg, 0);
        }
    }

    bool actAsMain = !sysState.hasRight;

    // If this board is the main board, periodically broadcast its ID so
    // satellites can compute their relative octave for display.
    if (actAsMain) {
        // Keep mainKeyboardId in sync on the main board itself
        sysState.mainKeyboardId = sysState.keyboardId;

        // Send {M} every ~1 s (50 × 20 ms cycles) to avoid flooding the CAN bus
        static uint8_t mBroadcastCounter = 0;
        if (++mBroadcastCounter >= 5) {
            // every 100 ms
            mBroadcastCounter = 0;
            if (sysState.hasLeft || sysState.hasRight) {
                uint8_t mMsg[8] = {'M', sysState.keyboardId, 0, 0, 0, 0, 0, 0};
                xQueueSend(msgOutQ, mMsg, 0); // non-blocking; drop if queue full
            }
        }
    }

    // Compare with previous state to detect changes and send messages
#ifdef TEST_SCANKEYS
    // Worst-case test: generate 12 key press messages every iteration
    for (int i = 0; i < KEYS_PER_KEYBOARD; i++) {
        uint8_t TX_Message[8] = {'P', (uint8_t) i, sysState.keyboardId, 0, 0, 0, 0, 0};
        xQueueSend(msgInQ, TX_Message, 0); // Non-blocking for test
    }
#else
    uint32_t changedKeys = currentKeyMask ^ previousKeyMask;
    for (int i = 0; i < KEYS_PER_KEYBOARD; i++) {
        // Check if bit i is set in changedKeys (key state changed)
        if ((changedKeys >> i) & 1) {
            // Extract current key state: 1 = pressed, 0 = released
            bool keyPressed = (currentKeyMask >> i) & 1;
            uint8_t msgType = keyPressed ? 'P' : 'R';
            uint8_t TX_Message[8] = {
                msgType, (uint8_t) i, sysState.keyboardId, 0, 0, 0, 0, 0
            };

            // Satellites send key events over CAN to the main board.
            // The main board (or a standalone board) processes keys locally.
            if (!actAsMain && (sysState.hasLeft || sysState.hasRight)) {
                xQueueSend(msgOutQ, TX_Message, pdMS_TO_TICKS(20));
            }

            if (actAsMain) {
                xQueueSend(msgInQ, TX_Message, pdMS_TO_TICKS(10));
            }
        }
    }
    // Update previous mask for next iteration
    previousKeyMask = currentKeyMask;

    // Lock-free atomic store for key state
    atomicKeyMask.store(currentKeyMask, std::memory_order_relaxed);

    // Mutex only for pitchBendEnabled toggle
    // Joystick button still in scanKeysTask
    if (requestPitchBendToggle) {
        MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
        if (lock) {
            sysState.pitchBendEnabled = !sysState.pitchBendEnabled;
        }
    }
#endif

    // Update DSP parameters outside mutex to avoid blocking ISR
    // Only main board (no right neighbor) should generate sound
    if (actAsMain) {
        dspUpdateParams();
    }

    // Reset local state for next cycle
    requestPitchBendToggle = false;
#ifndef TEST_SCANKEYS
  }
#endif
}

// ============================================================================
// Pitch Bend Task - 50ms interval
// Processes joystick Y-axis for pitch bend control
// Priority: 1 (lower than scanKeysTask to not interfere)
// ============================================================================
[[noreturn]] void pitchBendTask(void *pvParameters) {
    // Static variables persist across task invocations
    static int16_t currentBendCents = 0;
    static uint32_t lastUpdateMs = 0;

    uint32_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        // Read pitch bend enabled state
        bool pbEnabled = false;
        {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                pbEnabled = sysState.pitchBendEnabled;
            }
        }

        int16_t joyY = cachedJoyY;
        int16_t joyCenter = JOY_CENTER_Y;
        int16_t threshold = JOY_THRESHOLD;

        uint8_t newPitchBend = PITCH_BEND_CENTER;
        int8_t bendSemitones = 0;

        if (pbEnabled) {
            uint32_t now = millis();

            if (now - lastUpdateMs > 50) {
                lastUpdateMs = now;

                int16_t joyDelta = 0;
                if (joyY < JOY_CENTER_Y - JOY_THRESHOLD) {
                    joyDelta = joyY - (JOY_CENTER_Y - JOY_THRESHOLD);
                } else if (joyY > JOY_CENTER_Y + JOY_THRESHOLD) {
                    joyDelta = joyY - (JOY_CENTER_Y + JOY_THRESHOLD);
                }

                currentBendCents += joyDelta / 35;
                if (currentBendCents < -200) currentBendCents = -200;
                if (currentBendCents > 200) currentBendCents = 200;
            }

            int16_t rawBend = PITCH_BEND_CENTER + (currentBendCents * 128 / 200);
            if (rawBend < 0) rawBend = 0;
            if (rawBend > 255) rawBend = 255;
            newPitchBend = static_cast<uint8_t>(rawBend);
            bendSemitones = (currentBendCents + 50) / 100;
        } else {
            currentBendCents = 0;
            newPitchBend = PITCH_BEND_CENTER;
            bendSemitones = 0;
        }

        __atomic_store_n(&pitchBendValue, newPitchBend, __ATOMIC_RELAXED);

        {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                sysState.displayPitchBend = bendSemitones;
            }
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// Scan Joystick Task - 100ms interval
// Reads joystick analog values (X/Y) for navigation
// Priority: 1 (same as pitchBendTask, lower than scanKeysTask)
// ============================================================================
void scanJoystickTask(void *_) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // 100ms = 10Hz
    xLastWakeTime = xTaskGetTickCount();

    while (true) {
        int joyY = analogRead(JOYY_PIN);

        // Update sysState with mutex protection
        {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                sysState.joystickY = joyY;
            }
        }

        // Call navigation update with current pitch bend state
        bool pbEnabled = false;
        {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                pbEnabled = sysState.pitchBendEnabled;
            }
        }
        navUpdate(joyY, pbEnabled);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
