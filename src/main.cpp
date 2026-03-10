#include <Arduino.h>
#include <U8g2lib.h>
#include <STM32FreeRTOS.h>
#include "globals.h"
#include "constants.h"
#include "hw.h"
#include "dsp.h"
#include "ui.h"
#include "patch_memory.h"
#include <ES_CAN.h>

// Define shared state
SystemState sysState;

// Timer for audio ISR
HardwareTimer* sampleTimer = nullptr;

QueueHandle_t msgInQ;
QueueHandle_t msgOutQ;
SemaphoreHandle_t CAN_TX_Semaphore;

void CAN_RX_ISR (void) {
#if (NODE_MODE == MODE_SENDER_ONLY)
    uint8_t RX_Message_ISR[8];
    uint32_t ID;
    CAN_RX(ID, RX_Message_ISR);
#else
    uint8_t RX_Message_ISR[8];
    uint32_t ID;
    CAN_RX(ID, RX_Message_ISR);
    xQueueSendFromISR(msgInQ, RX_Message_ISR, NULL);
#endif
}

void CAN_TX_ISR (void) {
    xSemaphoreGiveFromISR(CAN_TX_Semaphore, NULL);
}

void decodeTask(void * pvParameters) {
    uint8_t RX_Message[8];
    while (1) {
        xQueueReceive(msgInQ, RX_Message, portMAX_DELAY);
#if (NODE_MODE != MODE_SENDER_ONLY)
        if (sysState.mutex != NULL) {
            if (xSemaphoreTake(sysState.mutex, portMAX_DELAY) == pdTRUE) {
                for(int i=0; i<8; i++) sysState.RX_Message[i] = RX_Message[i];
                if (RX_Message[0] == 'P') sysState.pressedKey = RX_Message[1] * 12 + RX_Message[2];
                else if (RX_Message[0] == 'R') sysState.pressedKey = -1;
                else if (RX_Message[0] == 'H') sysState.lastHandshakePos = RX_Message[1];
                xSemaphoreGive(sysState.mutex);
            }
        }
#endif
    }
}

void CAN_TX_Task(void * pvParameters) {
    uint8_t msgOut[8];
    while (1) {
        xQueueReceive(msgOutQ, msgOut, portMAX_DELAY);
        if (xSemaphoreTake(CAN_TX_Semaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
#if (NODE_MODE != MODE_RECEIVER_ONLY)
            CAN_TX(0x123, msgOut);
#endif
        }
    }
}



// Override default SystemClock_Config to disable LSE, which might be missing/broken and causing a hang
extern "C" void SystemClock_Config(void) {
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {};
  RCC_OscInitTypeDef RCC_OscInitStruct = {};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {};

  /* MSI is enabled after System reset, activate PLL with MSI as source */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI; // Removed LSE
  RCC_OscInitStruct.LSEState = RCC_LSE_OFF;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    while(1);
  }

  /* Initializes the CPU, AHB and APB buses clocks */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    while(1);
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLLSAI1;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_MSI;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 24;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_48M2CLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
    while(1);
  }
  /* Configure the main internal regulator output voltage */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
    while(1);
  }
}

void setup() {
    // VERY FIRST THING: Blink to prove we reached setup
    pinMode(LED_BUILTIN, OUTPUT);
    for (int i = 0; i < 6; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        for(volatile int j=0; j<100000; j++);
        digitalWrite(LED_BUILTIN, LOW);
        for(volatile int j=0; j<100000; j++);
    }

    hwInit();

    // Boot Diagnostic: Blink LED 5 times rapidly to indicate we reached setup()
    for (int i = 0; i < 10; i++) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        for(volatile int j=0; j<500000; j++);
    }

    // Create mutex
    sysState.mutex = xSemaphoreCreateMutex();
    if(sysState.mutex == NULL) {
        while(1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            for(volatile int i=0; i<100000; i++);
        }
    }

    // Init state
    sysState.menuMode = false;
    sysState.activePage = PAGE_OSC;
    sysState.pressedKey = -1;
    sysState.currentPatchSlot = 0;
    sysState.patchDirty = false;
    sysState.viewMode = 0;  // Default to performance view
    sysState.isSenderNode = true;
    sysState.currentOctave = 5; // C4 is 60 -> 5*12=60
    sysState.lastHandshakePos = -1;
    for(int i=0; i<8; i++) sysState.RX_Message[i] = 0;

    dspInit();
    
    // Diagnostic toggle before uiInit
    digitalWrite(LED_BUILTIN, HIGH);
    uiInit();
    digitalWrite(LED_BUILTIN, LOW);
    
    patchMemoryInit();

    msgInQ = xQueueCreate(36, 8);
    msgOutQ = xQueueCreate(36, 8);
    CAN_TX_Semaphore = xSemaphoreCreateCounting(3, 3);

    if (msgInQ == NULL || msgOutQ == NULL || CAN_TX_Semaphore == NULL) {
        while(1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            for(volatile int i=0; i<100000; i++);
        }
    }

    CAN_Init(CAN_LOOPBACK);
    setCANFilter(0x123, 0x7ff);
    CAN_RegisterRX_ISR(CAN_RX_ISR);
    CAN_RegisterTX_ISR(CAN_TX_ISR);
    
    // Diagnostic before CAN_Start
    digitalWrite(LED_BUILTIN, HIGH);
    CAN_Start();
    digitalWrite(LED_BUILTIN, LOW);

    if (xTaskCreate(decodeTask, "decode", 256, NULL, 3, NULL) != pdPASS) {
        while(1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            for(volatile int i=0; i<100000; i++);
        }
    }
    if (xTaskCreate(CAN_TX_Task, "canTx", 256, NULL, 2, NULL) != pdPASS) {
        while(1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            for(volatile int i=0; i<100000; i++);
        }
    }

    // Configure sample timer for 22kHz
    sampleTimer = new HardwareTimer(TIM1);
    sampleTimer->setOverflow(SAMPLE_RATE, HERTZ_FORMAT);
    sampleTimer->attachInterrupt(sampleISR);
    // Timer will be resumed by an initialization task after the scheduler starts

    // Create tasks
    if (xTaskCreate(scanKeysTask, "scanKeys", 256, NULL, 2, &scanKeysHandle) != pdPASS) {
        while(1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            for(volatile int i=0; i<100000; i++);
        }
    }
    if (xTaskCreate(displayUpdateTask, "displayUpdate", 256, NULL, 1, NULL) != pdPASS) {
        while(1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            for(volatile int i=0; i<100000; i++);
        }
    }

    // Create a task to start the timer safely after the scheduler has started
    TaskHandle_t timerTaskHandle;
    xTaskCreate([](void* pvParameters) {
        sampleTimer->resume();
        vTaskDelete(NULL);
    }, "startTimer", 64, NULL, 4, &timerTaskHandle);

    if (timerTaskHandle == NULL) {
        while(1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            for(volatile int i=0; i<100000; i++);
        }
    }

    // Start scheduler
    vTaskStartScheduler();
}

void loop() {
    // Empty
}
