#include "hw.h"
#include "constants.h"
#include "ui.h" // For uiHandleKnobRotation
#include "dsp.h" // For dspUpdateParams

TaskHandle_t scanKeysHandle = NULL;

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

void scanKeysTask(void * pvParameters) {
    const TickType_t xFrequency = pdMS_TO_TICKS(20);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    static Knob knobs[4] = {Knob(0), Knob(1), Knob(2), Knob(3)};

    // Track keyboard ID (set during handshaking)
    static uint8_t localKeyboardId = 0;
    // Joystick navigation state
    static int8_t xDirection = 0;
    static uint8_t consecutiveReads = 0;
    static uint32_t navCooldown = 0;

    static int modulePosition = -1;
    static bool eastOut = true;
    static bool westOut = true;

    while(1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        std::bitset<32> localInputs;
        uint8_t knobCurrentStates[4] = {0, 0, 0, 0};
        
        // Scan key matrix rows 0-4
        for(int i = 0; i < 5; i++){
            setRow(i, true);  // Enable row for key scanning
            delayMicroseconds(3);
            std::bitset<4> cols = readCols();
            localInputs[i*4] = cols[0];
            localInputs[i*4+1] = cols[1];
            localInputs[i*4+2] = cols[2];
            localInputs[i*4+3] = cols[3];
            
            // Capture Knobs from rows 3 and 4
            if(i == 3){
                knobCurrentStates[3] = (cols[0] << 1) | cols[1];
                knobCurrentStates[2] = (cols[2] << 1) | cols[3];
            } else if (i == 4) {
                knobCurrentStates[1] = (cols[0] << 1) | cols[1];
                knobCurrentStates[0] = (cols[2] << 1) | cols[3];
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

        // Handshaking Logic
        if (modulePosition == -1 && millis() > 1000) {
            if (!westIn) {
                // Leftmost module OR we detected a neighbor turning off their east output
                if (sysState.lastHandshakePos == -1) {
                    // First module (leftmost)
                    modulePosition = 0;
                    localKeyboardId = 0;
                    eastOut = false; // Tell next module to the East
                    {
                        MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
                        if (lock) {
                            sysState.keyboardId = 0;
                            sysState.hasLeft = false;  // Leftmost has no left neighbor
                            sysState.hasRight = eastIn;
                        }
                    }
                    uint8_t TX_Message[8] = {'H', 0, 0, 0, 0, 0, 0, 0};
                    xQueueSend(msgOutQ, TX_Message, portMAX_DELAY);
                } else {
                    // We received a handshake message from left neighbor - we're the next in chain
                    modulePosition = sysState.lastHandshakePos + 1;
                    localKeyboardId = modulePosition;
                    eastOut = false; // Tell next module to the East
                    {
                        MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
                        if (lock) {
                            sysState.keyboardId = localKeyboardId;
                            sysState.hasLeft = true;   // We got here via handshake, so there's a left neighbor
                            sysState.hasRight = eastIn;
                        }
                    }
                    uint8_t TX_Message[8] = {'H', (uint8_t)modulePosition, 0, 0, 0, 0, 0, 0};
                    xQueueSend(msgOutQ, TX_Message, portMAX_DELAY);
                }
            }
        }
        
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

        // Menu Navigation - joystick controls
        // Joystick DOWN cycles through modes: Performance -> Scope -> Env -> Menu
        // Joystick LEFT/RIGHT in menu: change pages
        // Joystick UP: exit menu (go to Performance)
        
        // Read joystick analog
        int16_t joyX = analogRead(JOYX_PIN);
        int16_t joyY = analogRead(JOYY_PIN);
        uint32_t now = millis();

        // Mode cycling: Joystick DOWN moves to next mode
        static bool joyDownPressed = false;
        static uint32_t lastModeChange = 0;

        if (joyY > JOY_DOWN_THRESHOLD && !joyDownPressed) {
            // Joystick just moved down - cycle to next mode
            if (now - lastModeChange > 300) { // debounce
                MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
                if (lock) {
                    // Cycle: Performance(0) -> Scope(1) -> Env(2) -> Menu(3)
                    uint8_t newMode = (sysState.viewMode + 1) % 4;
                    sysState.menuMode = (newMode == 3);
                    sysState.viewMode = newMode;
                }
                joyDownPressed = true;
                lastModeChange = now;
            }
        } else if (joyY <= JOY_DOWN_THRESHOLD - 100) {
            // Released - allow next press
            joyDownPressed = false;
        }

        // Joystick UP in menu mode: exit menu to Performance
        static bool joyUpPressed = false;

        bool inMenu = false;
        {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
            if (lock) {
                inMenu = sysState.menuMode;
            }
        }

        if (inMenu && joyY < JOY_UP_THRESHOLD && !joyUpPressed) {
            // Joystick UP in menu - exit to Performance
            if (now - lastModeChange > 300) {
                MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
                if (lock) {
                    sysState.menuMode = false;
                    sysState.viewMode = 0; // Performance
                }
                joyUpPressed = true;
                lastModeChange = now;
            }
        } else if (joyY >= JOY_UP_THRESHOLD + 100) {
            joyUpPressed = false;
        }

        // Page navigation within menu mode
        if (inMenu) {
            int8_t newXDir = 0;

            if(joyX < JOY_CENTER_X - JOY_THRESHOLD) newXDir = -1;
            else if(joyX > JOY_CENTER_X + JOY_THRESHOLD) newXDir = +1;

            if(newXDir != 0 && newXDir == xDirection) consecutiveReads++;
            else consecutiveReads = 0;

            xDirection = newXDir;

            if(consecutiveReads >= 3 && now > navCooldown) {
                MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
                if (lock) {
                    // Left / Right changes active page
                    if(xDirection < 0 && sysState.activePage > 0) {
                        sysState.activePage = (MenuPage)(sysState.activePage - 1);
                        navCooldown = now + 200;
                    }
                    else if(xDirection > 0 && sysState.activePage < PAGE_COUNT - 1) {
                        sysState.activePage = (MenuPage)(sysState.activePage + 1);
                        navCooldown = now + 200;
                    }
                }
                consecutiveReads = 0;
            }
        }

        // Knob decoding for all 4 knobs
        for (int i = 0; i < 4; i++) {
            int8_t direction = knobs[i].update(knobCurrentStates[i]);
            if (direction != 0) {
                uiHandleKnobRotation(i, direction);
            }
        }

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
