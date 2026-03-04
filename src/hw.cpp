#include "hw.h"
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

void setRow(uint8_t rowIdx) {
    digitalWrite(REN_PIN, LOW);
    digitalWrite(RA0_PIN, rowIdx & 0x01);
    digitalWrite(RA1_PIN, (rowIdx >> 1) & 0x01);
    digitalWrite(RA2_PIN, (rowIdx >> 2) & 0x01);
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
    
    static uint32_t lastJoyPressTime = 0;
    static bool prevJoyPressed = false;
    static uint32_t joyPressStartTime = 0;
    static bool longPressDetected = false;

    // Joystick navigation state
    const int16_t centerX = 540;
    const int16_t centerY = 500;
    const int16_t threshold = 150;
    static int8_t xDirection = 0;
    static int8_t yDirection = 0;
    static uint8_t consecutiveReads = 0;
    static uint32_t navCooldown = 0;

    while(1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        std::bitset<32> localInputs;
        uint8_t knobCurrentStates[4] = {0, 0, 0, 0};
        
        // Scan key matrix rows 0-4
        for(int i = 0; i < 5; i++){
            setRow(i);
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
        
        // Read joystick button (row 5, col 2)
        setRow(5);
        delayMicroseconds(3);
        std::bitset<4> cols5 = readCols();
        localInputs[20] = cols5[0];
        localInputs[21] = cols5[1];
        localInputs[22] = cols5[2]; // Joystick S button
        localInputs[23] = cols5[3];
        
        // Determine pressed key (0-11) -> Map to MIDI note 60-71 (C4-B4)
        int pressedKey = -1;
        for(int i = 0; i < 12; i++){
            if(!localInputs[i]){
                pressedKey = 60 + i;
                break;
            }
        }
        
        // Read joystick analog
        int16_t joyX = analogRead(JOYX_PIN);
        int16_t joyY = analogRead(JOYY_PIN);
        bool joyPressed = !localInputs[22];

        uint32_t now = millis();

        // Joystick Button - Short press toggles Menu Mode, Long press toggles View Mode
        if(joyPressed && !prevJoyPressed) {
            // Button just pressed
            joyPressStartTime = now;
            longPressDetected = false;
        } else if (joyPressed && !longPressDetected) {
            // Button held - check for long press (1 second)
            if (now - joyPressStartTime > 1000) {
                longPressDetected = true;
                // Toggle view mode (Performance/Scope/Envelope)
                if(sysState.mutex != NULL){
                    if(xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE){
                        sysState.viewMode = (sysState.viewMode + 1) % 3;
                        xSemaphoreGive(sysState.mutex);
                    }
                }
            }
        } else if (!joyPressed && prevJoyPressed) {
            // Button just released - short press if not long press
            if (!longPressDetected && (now - joyPressStartTime < 1000)) {
                if (now - lastJoyPressTime > 200) {
                    lastJoyPressTime = now;
                    if(sysState.mutex != NULL){
                        if(xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE){
                            sysState.menuMode = !sysState.menuMode;
                            xSemaphoreGive(sysState.mutex);
                        }
                    }
                }
            }
        }
        prevJoyPressed = joyPressed;

        // Menu Navigation
        bool inMenu = false;
        if (sysState.mutex != NULL) {
            if (xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                inMenu = sysState.menuMode;
                xSemaphoreGive(sysState.mutex);
            }
        }

        if (inMenu) {
            int8_t newXDir = 0;
            int8_t newYDir = 0;
            
            if(joyX < centerX - threshold) newXDir = -1;
            else if(joyX > centerX + threshold) newXDir = +1;
            
            if(joyY < centerY - threshold) newYDir = -1; // Up
            else if(joyY > centerY + threshold) newYDir = +1; // Down
            
            if(newXDir != 0 && newXDir == xDirection) consecutiveReads++;
            else if(newYDir != 0 && newYDir == yDirection) consecutiveReads++;
            else consecutiveReads = 0;
            
            xDirection = newXDir;
            yDirection = newYDir;
            
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
