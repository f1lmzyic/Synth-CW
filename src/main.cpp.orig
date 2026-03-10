#include <Arduino.h>
#include <U8g2lib.h>
#include <STM32FreeRTOS.h>
#include "globals.h"
#include "constants.h"
#include "hw.h"
#include "dsp.h"
#include "ui.h"
#include "patch_memory.h"

// Define shared state
SystemState sysState;

// Timer for audio ISR
HardwareTimer sampleTimer(TIM1);

void setup() {
    hwInit();

    // Create mutex
    sysState.mutex = xSemaphoreCreateMutex();
    if(sysState.mutex == NULL) {
        while(1); // Halt
    }

    // Init state
    sysState.menuMode = false;
    sysState.activePage = PAGE_OSC;
    sysState.pressedKey = -1;
    sysState.currentPatchSlot = 0;
    sysState.patchDirty = false;
    sysState.viewMode = 0;  // Default to performance view

    dspInit();
    uiInit();
    patchMemoryInit();

    // Configure sample timer for 22kHz
    sampleTimer.setOverflow(SAMPLE_RATE, HERTZ_FORMAT);
    sampleTimer.attachInterrupt(sampleISR);
    sampleTimer.resume();

    // Create tasks
    xTaskCreate(scanKeysTask, "scanKeys", 256, NULL, 2, &scanKeysHandle);
    xTaskCreate(displayUpdateTask, "displayUpdate", 256, NULL, 1, NULL);

    // Start scheduler
    vTaskStartScheduler();
}

void loop() {
    // Empty
}
