#include "hw.h"
#include "constants.h"
#include "ui.h" // For uiHandleKnobRotation
#include "dsp.h" // For dspUpdateParams

TaskHandle_t scanKeysHandle = NULL;

static struct {
    bool downPressed = false;
    bool upPressed = false;
    int8_t xDirection = 0;
    uint8_t consecutiveReads = 0;
    uint32_t lastModeChange = 0;
    uint32_t navCooldown = 0;
} joystickState;

static void handleJoystickNavigation(int16_t joyX, int16_t joyY, uint32_t now) {
    // Mode cycling: Joystick DOWN moves to next mode
    if (joyY > JOY_DOWN_THRESHOLD && !joystickState.downPressed) {
        if (now - joystickState.lastModeChange > 300) {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                // Cycle: Performance(0) -> Scope(1) -> Env(2) -> Menu(3)
                uint8_t newMode = (sysState.viewMode + 1) % 4;
                sysState.menuMode = (newMode == 3);
                sysState.viewMode = newMode;
            }
            joystickState.downPressed = true;
            joystickState.lastModeChange = now;
        }
    } else if (joyY <= JOY_DOWN_THRESHOLD - 100) {
        joystickState.downPressed = false;
    }

    // Check menu mode
    bool inMenu = false;
    {
        MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
        if (lock) {
            inMenu = sysState.menuMode;
        }
    }

    // Joystick UP in menu mode: exit to Performance
    if (inMenu && joyY < JOY_UP_THRESHOLD && !joystickState.upPressed) {
        if (now - joystickState.lastModeChange > 300) {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                sysState.menuMode = false;
                sysState.viewMode = 0;
            }
            joystickState.upPressed = true;
            joystickState.lastModeChange = now;
        }
    } else if (joyY >= JOY_UP_THRESHOLD + 100) {
        joystickState.upPressed = false;
    }

    // Page navigation within menu mode
    if (inMenu) {
        int8_t newXDir = 0;
        if (joyX < JOY_CENTER_X - JOY_THRESHOLD) newXDir = -1;
        else if (joyX > JOY_CENTER_X + JOY_THRESHOLD) newXDir = +1;

        if (newXDir != 0 && newXDir == joystickState.xDirection) {
            joystickState.consecutiveReads++;
        } else {
            joystickState.consecutiveReads = 0;
        }
        joystickState.xDirection = newXDir;

        if (joystickState.consecutiveReads >= 3 && now > joystickState.navCooldown) {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                if (joystickState.xDirection < 0 && sysState.activePage > 0) {
                    sysState.activePage = (MenuPage)(sysState.activePage - 1);
                    joystickState.navCooldown = now + 200;
                } else if (joystickState.xDirection > 0 && sysState.activePage < PAGE_COUNT - 1) {
                    sysState.activePage = (MenuPage)(sysState.activePage + 1);
                    joystickState.navCooldown = now + 200;
                }
            }
            joystickState.consecutiveReads = 0;
        }
    }
}

// ============================================================================
// Multi-keyboard connection handling
// ============================================================================
static void handleConnectionChange(bool westIn, bool eastIn, uint32_t now) {
    // Reset handshake state to allow re-negotiation
    sysState.eastOut = true;
    sysState.lastHandshakePos = -1;
    sysState.lastConnectionChangeTime = now;

    // Clear pressed keys from disconnected keyboards
    MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
    if (lock) {
        if (!westIn && sysState.prevWestIn) {
            // Left neighbor disconnected - clear keys from left keyboards
            uint16_t maxKeyToRemove = sysState.keyboardId * KEYS_PER_KEYBOARD;
            for (auto it = sysState.pressedKeys.begin(); it != sysState.pressedKeys.end(); ) {
                if (*it < maxKeyToRemove) {
                    it = sysState.pressedKeys.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    sysState.prevWestIn = westIn;
    sysState.prevEastIn = eastIn;
}

// ============================================================================
// Perform handshake to determine keyboard position
// ============================================================================
static void performHandshake(bool westIn, bool eastIn) {
    if (westIn) return; // Wait for left neighbor to be ready

    sysState.eastOut = false; // Signal right neighbor we're ready

    MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
    if (lock) {
        sysState.hasLeft = (sysState.lastHandshakePos >= 0);
        sysState.hasRight = eastIn;

        // Determine keyboard position in chain
        if (!sysState.hasLeft) {
            sysState.keyboardId = 0;
        } else {
            sysState.keyboardId = sysState.lastHandshakePos + 1;
        }
    }

    // Broadcast position for right neighbor
    uint8_t pos = sysState.hasLeft ? sysState.lastHandshakePos + 1 : 0;
    uint8_t TX_Message[8] = {'H', pos, 0, 0, 0, 0, 0, 0};
    xQueueSend(msgOutQ, TX_Message, portMAX_DELAY);
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
    bool readyToHandshake = (now > HANDSHAKE_STARTUP_DELAY) &&
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
    std::bitset<4> result;
    result[0] = digitalRead(C0_PIN);
    result[1] = digitalRead(C1_PIN);
    result[2] = digitalRead(C2_PIN);
    result[3] = digitalRead(C3_PIN);
    return result;
}

void scanKeysTask(void *pvParameters) {
    const TickType_t xFrequency = pdMS_TO_TICKS(20);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    static Knob knobs[4];

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        std::bitset<32> localInputs;

        // Scan key matrix rows 0-4
        for (int i = 0; i < 5; i++) {
            setRow(i, true); // Enable row for key scanning
            delayMicroseconds(3);
            std::bitset<4> cols = readCols();
            localInputs[i * 4] = cols[0];
            localInputs[i * 4 + 1] = cols[1];
            localInputs[i * 4 + 2] = cols[2];
            localInputs[i * 4 + 3] = cols[3];

            // Decode knobs from rows 3 and 4
            if (i == 3) {
                int8_t dir3 = knobs[3].update((cols[0] << 1) | cols[1]);
                int8_t dir2 = knobs[2].update((cols[2] << 1) | cols[3]);
                if (dir3 != 0) uiHandleKnobRotation(3, dir3);
                if (dir2 != 0) uiHandleKnobRotation(2, dir2);
            } else if (i == 4) {
                int8_t dir1 = knobs[1].update((cols[0] << 1) | cols[1]);
                int8_t dir0 = knobs[0].update((cols[2] << 1) | cols[3]);
                if (dir1 != 0) uiHandleKnobRotation(1, dir1);
                if (dir0 != 0) uiHandleKnobRotation(0, dir0);
            }
        }

        // Read joystick button (row 5, col 2) and West input (row 5, col 3)
        setRow(5, true);
        delayMicroseconds(3);
        std::bitset<4> cols5 = readCols();
        localInputs[20] = cols5[0];
        localInputs[21] = cols5[1];
        localInputs[22] = cols5[2]; // Joystick S button
        localInputs[23] = cols5[3]; // West Input
        bool westIn = !cols5[3];

        // Read East input (row 6, col 3)
        setRow(6, sysState.eastOut);
        delayMicroseconds(3);
        std::bitset<4> cols6 = readCols();
        bool eastIn = !cols6[3]; // East Input
        localInputs[24] = cols6[0];
        localInputs[25] = cols6[1];
        localInputs[26] = cols6[2];
        localInputs[27] = cols6[3];

        // Hot-plug connection detection and handshake
        updateConnectionState(westIn, eastIn);

        // ============================================================================
        // Multi-key detection - detect ALL pressed keys (0-11)
        // ============================================================================
        // Track which keys are currently pressed (boolean array)
        static bool keysPressed[KEYS_PER_KEYBOARD] = {false};
        static bool keysPrevPressed[KEYS_PER_KEYBOARD] = {false};

        // Scan all 12 keys and build pressed keys array
        for (int i = 0; i < KEYS_PER_KEYBOARD; i++) {
            // localInputs[i] == 0 means key is pressed (active low)
            keysPressed[i] = !localInputs[i];
        }

        // Compare with previous state to detect changes and send messages
        for (int i = 0; i < KEYS_PER_KEYBOARD; i++) {
            if (keysPrevPressed[i] != keysPressed[i]) {
                uint8_t msgType = keysPressed[i] ? 'P' : 'R';
                uint8_t TX_Message[8] = {msgType, (uint8_t)i, sysState.keyboardId, 0, 0, 0, 0, 0};

                // Send on CAN if connected to other keyboards
                if (sysState.hasLeft || sysState.hasRight) {
                    xQueueSend(msgOutQ, TX_Message, portMAX_DELAY);
                }

                // Process locally if this keyboard plays audio (rightmost or standalone)
                // Rightmost = has no right neighbor
                if (!sysState.hasRight) {
                    xQueueSend(msgInQ, TX_Message, portMAX_DELAY);
                }

                keysPrevPressed[i] = keysPressed[i];
            }
        }

        // Joystick navigation
        handleJoystickNavigation(analogRead(JOYX_PIN), analogRead(JOYY_PIN), millis());

        // Update global state
        {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                sysState.inputs = localInputs;
            }
        }

        dspUpdateParams();
    }
}
