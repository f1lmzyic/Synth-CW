#include "constants.h"
#include "dsp.h"
#include "globals.h"
#include "hw.h"
#include "ui.h"
#include <Arduino.h>
#include <ES_CAN.h>
#include <STM32FreeRTOS.h>

SystemState sysState;

HardwareTimer *sampleTimer;

QueueHandle_t msgInQ;
QueueHandle_t msgOutQ;
SemaphoreHandle_t CAN_TX_Semaphore;

void fatalError() {
  while (true) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    for (volatile int i = 0; i < 100000; i++)
      ;
  }
}

void CAN_RX_ISR() {
  uint8_t RX_Message_ISR[8];
  uint32_t ID;
  CAN_RX(ID, RX_Message_ISR);
  xQueueSendFromISR(msgInQ, RX_Message_ISR, NULL);
}

void CAN_TX_ISR() { xSemaphoreGiveFromISR(CAN_TX_Semaphore, NULL); }

[[noreturn]] void decodeTask(void *pvParameters) {
  uint8_t RX_Message[8];
  while (true) {
    xQueueReceive(msgInQ, RX_Message, portMAX_DELAY);
    MutexGuard lock(sysState.mutex);
    if (lock) {
      uint8_t msgType = RX_Message[0];

      // Handshake messages processed by all keyboards (for position detection)
      if (msgType == 'H') {
        sysState.lastHandshakePos = RX_Message[1];
        continue;
      }

      // Key messages only processed by rightmost keyboard (the one that plays
      // audio) Rightmost = has no right neighbor
      if (sysState.hasRight) {
        continue;
      }

      uint8_t keyIndex = RX_Message[1];
      uint8_t keyboardId = RX_Message[2];

      // Calculate global key number (keyboardId * 12 + keyIndex)
      uint16_t globalKey = keyboardId * KEYS_PER_KEYBOARD + keyIndex;

      // Copy for UI debug display
      memcpy(sysState.RX_Message, RX_Message, 8);

      if (msgType == 'P') {
        // Key press - add to pressed keys array if not already present
        bool found = false;
        for (uint8_t i = 0; i < sysState.pressedKeyCount; i++) {
          if (sysState.pressedKeys[i] == globalKey) {
            found = true;
            break;
          }
        }
        if (!found && sysState.pressedKeyCount < MAX_PRESSED_KEYS) {
          sysState.pressedKeys[sysState.pressedKeyCount++] = globalKey;
        }
      } else if (msgType == 'R') {
        // Key release - remove from pressed keys array
        for (uint8_t i = 0; i < sysState.pressedKeyCount; i++) {
          if (sysState.pressedKeys[i] == globalKey) {
            // Shift remaining keys down
            for (uint8_t j = i; j < sysState.pressedKeyCount - 1; j++) {
              sysState.pressedKeys[j] = sysState.pressedKeys[j + 1];
            }
            sysState.pressedKeyCount--;
            break;
          }
        }
      }
    }
  }
}

[[noreturn]] void CAN_TX_Task(void *pvParameters) {
  uint8_t msgOut[8];
  while (true) {
    xQueueReceive(msgOutQ, msgOut, portMAX_DELAY);
    if (xSemaphoreTake(CAN_TX_Semaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
      // Always transmit - runtime logic in hw.cpp decides what to queue
      CAN_TX(0x123, msgOut);
    }
  }
}

// Override default SystemClock_Config to disable LSE, which might be
// missing/broken and causing a hang
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
    while (true)
      ;
  }

  /* Initializes the CPU, AHB and APB buses clocks */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    while (true)
      ;
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
    while (true)
      ;
  }
  /* Configure the main internal regulator output voltage */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
    while (true)
      ;
  }
}

void setup() {
  hwInit();

  sysState.mutex = xSemaphoreCreateMutex();
  if (sysState.mutex == nullptr) {
    fatalError();
  }

  // Init state
  sysState.menuMode = false;
  sysState.activePage = PAGE_OSC;
  sysState.viewMode = 0; // Default to performance view
  sysState.lastHandshakePos = -1;
  sysState.keyboardId = 0;   // Default keyboard ID
  sysState.octaveOffset = 0; // Default octave (middle C = C4)

  // Multi-keyboard: assume standalone until handshake determines otherwise
  sysState.hasLeft = false;
  sysState.hasRight =
      false; // No right neighbor = plays audio (standalone mode)
  sysState.prevWestIn = false;
  sysState.prevEastIn = false;
  sysState.eastOut = true;
  sysState.lastConnectionChangeTime = 0;

  // Initialize polyphony state
  sysState.pressedKeyCount = 0;
  memset(sysState.pressedKeys, 0xFF, sizeof(sysState.pressedKeys));

  dspInit();

  uiInit();

  msgInQ = xQueueCreate(36, 8);
  msgOutQ = xQueueCreate(36, 8);
  CAN_TX_Semaphore = xSemaphoreCreateCounting(3, 3);

  if (msgInQ == nullptr || msgOutQ == nullptr || CAN_TX_Semaphore == nullptr) {
    fatalError();
  }

  CAN_Init(CAN_LOOPBACK);
  setCANFilter(0x123, 0x7ff);
  CAN_RegisterRX_ISR(CAN_RX_ISR);
  CAN_RegisterTX_ISR(CAN_TX_ISR);

  CAN_Start();

  if (xTaskCreate(decodeTask, "decode", 256, nullptr, 3, NULL) != pdPASS) {
    fatalError();
  }
  if (xTaskCreate(CAN_TX_Task, "canTx", 256, nullptr, 2, nullptr) != pdPASS) {
    fatalError();
  }

  // Configure sample timer for 22kHz
  sampleTimer = new HardwareTimer(TIM1);
  sampleTimer->setOverflow(SAMPLE_RATE, HERTZ_FORMAT);
  sampleTimer->attachInterrupt(sampleISR);
  // Timer will be resumed by an initialization task after the scheduler starts

  // Create tasks
  if (xTaskCreate(scanKeysTask, "scanKeys", 256, nullptr, 2, &scanKeysHandle) !=
      pdPASS) {
    fatalError();
  }
  if (xTaskCreate(displayUpdateTask, "displayUpdate", 256, nullptr, 1,
                  nullptr) != pdPASS) {
    fatalError();
  }

  // Create a task to start the timer safely after the scheduler has started
  TaskHandle_t timerTaskHandle;
  xTaskCreate(
      [](void *pvParameters) {
        sampleTimer->resume();
        vTaskDelete(nullptr);
      },
      "startTimer", 64, nullptr, 4, &timerTaskHandle);

  if (timerTaskHandle == nullptr) {
    fatalError();
  }

  vTaskStartScheduler();
}

void loop() {}
