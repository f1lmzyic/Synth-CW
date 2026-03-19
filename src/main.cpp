#include "constants.h"
#include "dsp.h"
#include "globals.h"
#include "hw.h"
#include "ui.h"
#include <Arduino.h>
#include <ES_CAN.h>
#include <STM32FreeRTOS.h>

#if TEST_MODE
void timingAnalysis();
#endif

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

      // Handshake messages: only update our left-neighbor position if we
      // haven't completed our own handshake yet (eastOut still true).
      // This prevents boards from corrupting their position when they receive
      // {H} messages from boards to their RIGHT (which have higher IDs).
      if (msgType == 'H') {
        // Max-wins: only update if received position is higher than current.
        // Prevents left-satellite's {H,0} from overwriting main's {H,1}
        // at a right-side board that receives periodic broadcasts from all boards.
        if (sysState.eastOut && (int16_t)RX_Message[1] > sysState.lastHandshakePos) {
          sysState.lastHandshakePos = RX_Message[1];
        }
        continue;
      }

      // Main-board ID broadcast: all boards update so satellites know their
      // relative octave for display.
      // Priority resolves conflicts when boards hot-plug or boot simultaneously.
      if (msgType == 'M') {
        uint8_t incomingId = RX_Message[1];
        uint8_t incomingPriority = RX_Message[2];
        
        uint8_t myPriority = 0;
        if (sysState.hasLeft && sysState.hasRight) myPriority = 2;
        else if (millis() > 2000) myPriority = 1;

        bool iAmMain = (sysState.mainKeyboardId == sysState.keyboardId);
        
        // Accept if we are not currently main, or if the incoming message has a 
        // strictly higher priority (e.g. middle board > edge board), or if 
        // priorities tie but incoming has a higher keyboardId (tie-breaker for 2 boards)
        if (!iAmMain || incomingPriority > myPriority || 
            (incomingPriority == myPriority && incomingId > sysState.keyboardId)) {
          sysState.mainKeyboardId = incomingId;
        }
        continue;
      }

      // Key messages only processed by the main board (or standalone board)
      bool actAsMain = (sysState.mainKeyboardId == sysState.keyboardId);
      if (!actAsMain) {
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
  sysState.viewMode = 0;
  sysState.lastHandshakePos = -1;
  sysState.keyboardId = 0;
  sysState.mainKeyboardId = 0;
  sysState.octaveOffset = 0;

  sysState.hasLeft = false;
  sysState.hasRight = false;
  sysState.prevWestIn = false;
  sysState.prevEastIn = false;
  sysState.eastOut = true;
  sysState.lastConnectionChangeTime = 0;

  sysState.pressedKeyCount = 0;
  memset(sysState.pressedKeys, 0xFF, sizeof(sysState.pressedKeys));
  sysState.pitchBendEnabled = false;
  sysState.displayPitchBend = 0;

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

#if !TEST_MODE
  xTaskCreate(decodeTask, "decode", 256, nullptr, 3, NULL);
  xTaskCreate(CAN_TX_Task, "canTx", 256, nullptr, 2, nullptr);
#endif

  sampleTimer = new HardwareTimer(TIM1);
  sampleTimer->setOverflow(SAMPLE_RATE, HERTZ_FORMAT);
  sampleTimer->attachInterrupt(sampleISR);

#if !TEST_MODE
  xTaskCreate(scanKeysTask, "scanKeys", 256, nullptr, 2, &scanKeysHandle);
  xTaskCreate(pitchBendTask, "pitchBend", 256, nullptr, 1, &pitchBendHandle);
  xTaskCreate(displayUpdateTask, "displayUpdate", 256, nullptr, 1, nullptr);

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
#endif

#if TEST_MODE
  timingAnalysis();
#endif
}

void loop() {
  static uint32_t next = millis();
  static uint32_t count = 0;

  while (millis() < next);
  next += 100;

  digitalToggle(LED_BUILTIN);
}

#if TEST_MODE
void timingAnalysis() {
  Serial.begin(115200);
  while (!Serial) {}

  Serial.println("=== scanKeysTask Timing Analysis ===");
  Serial.print("TEST_ITERATIONS = ");
  Serial.println(TEST_ITERATIONS);
  Serial.println();

  // Component timing variables
  uint32_t gpioScanTime = 0;
  uint32_t mutexTime = 0;
  uint32_t canTime = 0;
  uint32_t pitchBendTime = 0;
  uint32_t dspUpdateTime = 0;
  uint32_t connectionTime = 0;

  // Measure complete scanKeysTask iteration
  uint32_t totalStart = micros();
  
  for (int iter = 0; iter < TEST_ITERATIONS; iter++) {
    uint32_t iterStart = micros();
    
    // === GPIO Matrix Scanning (rows 0-4: keys) ===
    uint32_t gpioStart = micros();
    std::bitset<32> localInputs;
    
    // Enable mux once for entire scan cycle
    enableMuxFast();
    setOutFast(true);
    
    for (int row = 0; row < 5; row++) {
      setRowFast(row);  // GPIO: ~5-10 cycles
      uint32_t cols = readColsFast();
      
      // Extract individual columns from bitmask
      localInputs[row * 4] = cols & 1;
      localInputs[row * 4 + 1] = (cols >> 1) & 1;
      localInputs[row * 4 + 2] = (cols >> 2) & 1;
      localInputs[row * 4 + 3] = (cols >> 3) & 1;
    }
    gpioScanTime += (micros() - gpioStart);
    
    // === Joystick button + West input (row 5) ===
    gpioStart = micros();
    setRowFast(5);
    uint32_t cols5 = readColsFast();
    localInputs[20] = cols5 & 1;
    localInputs[21] = (cols5 >> 1) & 1;
    localInputs[22] = (cols5 >> 2) & 1;  // Joystick S button
    localInputs[23] = (cols5 >> 3) & 1;  // West Input
    bool westIn = !((cols5 >> 3) & 1);
    bool joyButton = !((cols5 >> 2) & 1);
    gpioScanTime += (micros() - gpioStart);
    
    // === Pitch bend toggle (mutex operation) ===
    uint32_t mutexStart = micros();
    static bool prevJoyButton = false;
    if (joyButton && !prevJoyButton) {
      MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
      if (lock) {
        sysState.pitchBendEnabled = !sysState.pitchBendEnabled;
      }
    }
    prevJoyButton = joyButton;
    mutexTime += (micros() - mutexStart);
    
    // === East input (row 6) ===
    gpioStart = micros();
    setRowFast(6);
    setOutFast(sysState.eastOut);
    uint32_t cols6 = readColsFast();
    bool eastIn = !((cols6 >> 3) & 1);
    gpioScanTime += (micros() - gpioStart);
    
    // === Connection state update ===
    uint32_t connStart = micros();
    // Simulate updateConnectionState logic
    static bool prevWestIn = false;
    static bool prevEastIn = false;
    if ((westIn != prevWestIn) || (eastIn != prevEastIn)) {
      sysState.eastOut = true;
      sysState.lastConnectionChangeTime = millis();
    }
    prevWestIn = westIn;
    prevEastIn = eastIn;
    if (millis() > HANDSHAKE_STARTUP_DELAY && 
        millis() - sysState.lastConnectionChangeTime > HANDSHAKE_SETTLE_TIME &&
        sysState.eastOut) {
      sysState.eastOut = false;
    }
    connectionTime += (micros() - connStart);
    
    // === Multi-key detection ===
    static bool keysPressed[12] = {false};
    static bool keysPrevPressed[12] = {false};
    for (int i = 0; i < 12; i++) {
      keysPressed[i] = !localInputs[i];
      if (keysPrevPressed[i] != keysPressed[i]) {
        canTime += 5;  // Estimate ~5us per CAN message queue operation
        keysPrevPressed[i] = keysPressed[i];
      }
    }
    
    // === DSP parameter update ===
    uint32_t dspStart = micros();
    dspUpdateParams();
    dspUpdateTime += (micros() - dspStart);
  }
  
  uint32_t totalTime = micros() - totalStart;
  uint32_t avgTotal = totalTime / TEST_ITERATIONS;
  
  // Calculate averages
  uint32_t avgGpio = gpioScanTime / TEST_ITERATIONS;
  uint32_t avgMutex = mutexTime / TEST_ITERATIONS;
  uint32_t avgCan = canTime / TEST_ITERATIONS;
  uint32_t avgDsp = dspUpdateTime / TEST_ITERATIONS;
  uint32_t avgConnection = connectionTime / TEST_ITERATIONS;
  
  // Output results - GPIO version
  Serial.println("=== Component Breakdown ===");
  Serial.print("GPIO scanning (FAST register):   ");
  Serial.print(avgGpio);
  Serial.println(" us");
  Serial.println("  (No settling delays - instant access)");
  
  Serial.print("Mutex operations (x1 only):      ");
  Serial.print(avgMutex);
  Serial.println(" us");
  
  Serial.print("CAN message queue (estimated):   ");
  Serial.print(avgCan);
  Serial.println(" us");
  
  Serial.print("Connection state update:         ");
  Serial.print(avgConnection);
  Serial.println(" us");
  
  Serial.print("dspUpdateParams():               ");
  Serial.print(avgDsp);
  Serial.println(" us");
  
  Serial.println();
  Serial.println("=== OFFLOADED Tasks (not in scanKeysTask) ===");
  Serial.println("Knob decoding:    scanKnobsTask (50ms interval)");
  Serial.println("Joystick analog:  scanJoystickTask (100ms interval)");
  Serial.println("Pitch bend:       pitchBendTask (50ms interval)");
  
  Serial.println();
  Serial.println("=== Summary ===");
  Serial.print("scanKeysTask total:              ");
  Serial.print(avgTotal);
  Serial.print(" us (");
  Serial.print(avgTotal / 1000.0, 3);
  Serial.println(" ms)");
  
  // Target check
  Serial.println();
  if (avgTotal < 100) {
    Serial.print("TARGET MET: ");
    Serial.print(avgTotal);
    Serial.println(" us < 100 us (target)");
  } else {
    Serial.print("TARGET MISSED: ");
    Serial.print(avgTotal);
    Serial.println(" us >= 100 us (target)");
  }
  
  Serial.println();
  Serial.println("=== Other Tasks ===");
  
  // Test displayUpdate (one iteration)
  uint32_t startTime = micros();
  for (int i = 0; i < TEST_ITERATIONS; i++) {
    SystemState localState;
    {
      MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
      if (lock) {
        localState = sysState;
      }
    }
    u8g2->clearBuffer();
    u8g2->setFont(u8g2_font_5x7_tr);
    u8g2->setCursor(0, 10);
    u8g2->print("TEST");
    u8g2->sendBuffer();
  }
  uint32_t displayTime = (micros() - startTime) / TEST_ITERATIONS;
  Serial.print("displayUpdateTask:             ");
  Serial.print(displayTime);
  Serial.println(" us");
  
  // Test sampleISR
  startTime = micros();
  for (int i = 0; i < TEST_ITERATIONS; i++) {
    sampleISR();
  }
  uint32_t isrTime = (micros() - startTime) / TEST_ITERATIONS;
  Serial.print("sampleISR:                     ");
  Serial.print(isrTime);
  Serial.println(" us");
  
  Serial.println();
  Serial.println("=== Complete ===");
  Serial.print("Total WCET (all tasks):        ");
  Serial.print(avgTotal + displayTime + isrTime);
  Serial.println(" us");
  
  Serial.println();
  Serial.println("Halting - reset board to run normal firmware");
  
  while (1) {}
}
#endif
