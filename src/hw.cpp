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
    
    static uint8_t knobPrevStates[4] = {0, 0, 0, 0};
    static int8_t knobLastDirections[4] = {0, 0, 0, 0};

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
        setRow(5, westOut);
        delayMicroseconds(3);
        std::bitset<4> cols5 = readCols();
        localInputs[20] = cols5[0];
        localInputs[21] = cols5[1];
        localInputs[22] = cols5[2]; // Joystick S button
        localInputs[23] = cols5[3]; // West Input
        bool westIn = !cols5[3];
        
        // Read East input (row 6, col 3)
        setRow(6, eastOut);
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
                    if (!eastIn) {
                        // Standalone (no other keyboards)
                        if (sysState.mutex != NULL && xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                            sysState.currentOctave = 5;
                            sysState.keyboardId = 0;
                            xSemaphoreGive(sysState.mutex);
                        }
                    } else {
                        // Connected to more keyboards on the right
                        if (sysState.mutex != NULL && xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                            sysState.currentOctave = 4;
                            sysState.keyboardId = 0;
                            xSemaphoreGive(sysState.mutex);
                        }
                    }
                    uint8_t TX_Message[8] = {'H', 0, 0, 0, 0, 0, 0, 0};
                    xQueueSend(msgOutQ, TX_Message, portMAX_DELAY);
                } else {
                    // We received a handshake message from left neighbor - we're the next in chain
                    modulePosition = sysState.lastHandshakePos + 1;
                    localKeyboardId = modulePosition;
                    eastOut = false; // Tell next module to the East
                    if (sysState.mutex != NULL && xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                        // Octave decreases as we go right: keyboard 0=4, keyboard 1=3, keyboard 2=2
                        sysState.currentOctave = 4 - modulePosition;
                        sysState.keyboardId = localKeyboardId;
                        xSemaphoreGive(sysState.mutex);
                    }
                    uint8_t TX_Message[8] = {'H', (uint8_t)modulePosition, 0, 0, 0, 0, 0, 0};
                    xQueueSend(msgOutQ, TX_Message, portMAX_DELAY);
                }
            }
        }
        
        // ============================================================================
        // Multi-key detection - detect ALL pressed keys (0-11), not just first
        // ============================================================================
        int pressedKey = -1;

        // Track which keys are currently pressed (boolean array)
        static bool keysPressed[KEYS_PER_KEYBOARD] = {false};
        static bool keysPrevPressed[KEYS_PER_KEYBOARD] = {false};

        // Scan all 12 keys and build pressed keys array
        for(int i = 0; i < KEYS_PER_KEYBOARD; i++){
            // localInputs[i] == 0 means key is pressed (active low)
            keysPressed[i] = !localInputs[i];

            // Legacy support - first key becomes pressedKey
            if (keysPressed[i] && pressedKey == -1) {
                pressedKey = sysState.currentOctave * 12 + i;
            }
        }

        // Compare with previous state to detect changes and send messages
        for(int i = 0; i < KEYS_PER_KEYBOARD; i++) {
            if (keysPrevPressed[i] != keysPressed[i]) {
                uint8_t msgType = keysPressed[i] ? 'P' : 'R';
                uint8_t TX_Message[8] = {msgType, sysState.currentOctave, (uint8_t)i, localKeyboardId, 0, 0, 0, 0};
#if (NODE_MODE != MODE_RECEIVER_ONLY)
                xQueueSend(msgOutQ, TX_Message, portMAX_DELAY);
#endif
#if (NODE_MODE != MODE_SENDER_ONLY)
                if (!(CAN_LOOPBACK && (NODE_MODE == MODE_BIDIRECTIONAL))) {
                    xQueueSend(msgInQ, TX_Message, portMAX_DELAY);
                }
#endif
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
                if(sysState.mutex != NULL){
                    if(xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE){
                        // Cycle: Performance(0) -> Scope(1) -> Env(2) -> Menu(3)
                        uint8_t newMode = (sysState.viewMode + 1) % 4;
                        
                        // If entering menu mode, set menuMode = true
                        if (newMode == 3) {
                            sysState.menuMode = true;
                        } else {
                            sysState.menuMode = false;
                        }
                        
                        sysState.viewMode = newMode;
                        xSemaphoreGive(sysState.mutex);
                    }
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
        if (sysState.mutex != NULL) {
            if (xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                inMenu = sysState.menuMode;
                xSemaphoreGive(sysState.mutex);
            }
        }
        
        if (inMenu && joyY < JOY_UP_THRESHOLD && !joyUpPressed) {
            // Joystick UP in menu - exit to Performance
            if (now - lastModeChange > 300) {
                if(sysState.mutex != NULL){
                    if(xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE){
                        sysState.menuMode = false;
                        sysState.viewMode = 0; // Performance
                        xSemaphoreGive(sysState.mutex);
                    }
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
                if (sysState.mutex != NULL) {
                    if (xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                        
                        // Left / Right changes active page
                        if(xDirection < 0 && sysState.activePage > 0) {
                            sysState.activePage = (MenuPage)(sysState.activePage - 1);
                            navCooldown = now + 200;
                        } 
                        else if(xDirection > 0 && sysState.activePage < PAGE_COUNT - 1) {
                            sysState.activePage = (MenuPage)(sysState.activePage + 1);
                            navCooldown = now + 200;
                        }

                        xSemaphoreGive(sysState.mutex);
                    }
                }
                consecutiveReads = 0;
            }
        }

        // Knob decoding for all 4 knobs
        for (int i = 0; i < 4; i++) {
            if(knobCurrentStates[i] != knobPrevStates[i]){
                int8_t direction = 0;
                uint8_t transition = (knobPrevStates[i] << 2) | knobCurrentStates[i];
                switch(transition){
                    case 0b0001: case 0b1110: direction = 1; break;
                    case 0b0100: case 0b1011: direction = -1; break;
                    case 0b0011: case 0b1100: case 0b0101: case 0b1010:
                        direction = knobLastDirections[i]; break;
                    default: direction = 0; break;
                }
                if(direction != 0){
                    uiHandleKnobRotation(i, direction);
                    knobLastDirections[i] = direction;
                }
                knobPrevStates[i] = knobCurrentStates[i];
            }
        }

        // Update global state
        if(sysState.mutex != NULL){
            if(xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE){
                sysState.inputs = localInputs;
                sysState.pressedKey = pressedKey;
                xSemaphoreGive(sysState.mutex);
            }
        }
        
        dspUpdateParams();
    }
}
