---
source: FreeRTOS best practices + USBComposite_stm32f1 analysis
library: FreeRTOS on STM32
package: stm32-freertos-usb-midi
topic: Task priorities, queue sizes, and stack requirements for USB MIDI
fetched: 2026-03-13T00:00:00Z
official_docs: https://www.freertos.org/
---

# FreeRTOS Task Handling for USB/MIDI on STM32

## Task Architecture for MIDI Keyboard

### Recommended Task Structure
```
┌─────────────────────────────────────────────────────┐
│                    Main Loop                        │
│              (Idle task / low priority)             │
└─────────────────────────────────────────────────────┘
                          │
        ┌─────────────────┼─────────────────┐
        │                 │                 │
        ▼                 ▼                 ▼
┌───────────────┐ ┌───────────────┐ ┌───────────────┐
│  Key Scan     │ │  USB Send     │ │  LED/Display  │
│  Task         │ │  Task         │ │  Task         │
│  (High Pri)   │ │  (Med Pri)    │ │  (Low Pri)    │
└───────────────┘ └───────────────┘ └───────────────┘
        │                 │
        │                 │
        ▼                 ▼
┌───────────────┐ ┌───────────────┐
│ Key Event     │ │ USB Send      │
│ Queue         │ │ Queue         │
│ (16-32 items) │ │ (8-16 items)  │
└───────────────┘ └───────────────┘
```

---

## Task Priority Configuration

### Priority Assignments (configMAX_PRIORITIES = 5)
| Task | Priority | Rationale |
|------|----------|-----------|
| Key Scan Task | 4 (High) | Must respond quickly to key presses |
| USB Send Task | 3 (Medium-High) | Must drain queue before buffer fills |
| USB Poll Task | 3 (Medium-High) | Must process incoming MIDI |
| LED/Display Task | 2 (Medium) | Visual feedback, not time-critical |
| Idle Task | 0-1 (Low) | Background processing |

### Critical: USB Interrupt Priority
```c
// In FreeRTOSConfig.h
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

// USB interrupt must be ABOVE this threshold
// to be unaffected by FreeRTOS critical sections
NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 3);  // Higher priority!
NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
```

**WARNING:** If USB interrupt priority is too low:
- USB interrupts get masked during critical sections
- USB buffer doesn't get drained in time
- Buffer overflow → device freeze

---

## Queue Configuration

### Key Event Queue
```c
typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t pressed;  // 1 = press, 0 = release
    uint32_t timestamp;
} KeyEvent_t;

// Queue configuration
#define KEY_QUEUE_SIZE 32  // Hold up to 32 key events
#define KEY_QUEUE_ITEM_SIZE sizeof(KeyEvent_t)

QueueHandle_t xKeyEventQueue = xQueueCreate(
    KEY_QUEUE_SIZE, 
    KEY_QUEUE_ITEM_SIZE
);
```

**Sizing rationale:**
- 88-key keyboard worst case: All keys pressed rapidly
- Each key = 1 event (press or release)
- 32 events = ~10ms of continuous playing at 3000 events/sec
- Prevents queue overflow during USB send delays

### USB Send Queue
```c
typedef struct {
    uint8_t data[4];  // USB MIDI Event Packet
    uint8_t length;
} USBSendItem_t;

#define USB_SEND_QUEUE_SIZE 16
#define USB_SEND_ITEM_SIZE sizeof(USBSendItem_t)

QueueHandle_t xUSBSendQueue = xQueueCreate(
    USB_SEND_QUEUE_SIZE,
    USB_SEND_ITEM_SIZE
);
```

**Sizing rationale:**
- 16 items = 64 bytes of MIDI data
- Matches USB MIDI buffer size (128 bytes total, 64 TX)
- Provides backpressure when USB is slow

---

## Stack Size Requirements

### Minimum Stack Sizes (STM32F1, 20KB RAM)
| Task | Minimum Stack | Recommended Stack |
|------|--------------|-------------------|
| Key Scan Task | 256 words (1KB) | 512 words (2KB) |
| USB Send Task | 256 words (1KB) | 512 words (2KB) |
| USB Poll Task | 256 words (1KB) | 512 words (2KB) |
| LED Task | 128 words (512B) | 256 words (1KB) |
| Idle Hook | 128 words (512B) | 256 words (1KB) |

### Stack Calculation
```
Base stack (context save): ~20 words
Function call depth: ~10 words per level × 5 levels = 50 words
Local variables: ~50 words
USB structures: ~50 words
Safety margin: 50%
─────────────────────────────
Total: ~200 words minimum
```

### Stack Overflow Detection
```c
// Enable in FreeRTOSConfig.h
#define configCHECK_FOR_STACK_OVERFLOW 2

// Implement hook
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    // Log error, blink LED, halt
    while(1) {
        toggleErrorLED();
    }
}
```

---

## Task Implementation Examples

### Key Scan Task
```c
void vKeyScanTask(void *pvParameters) {
    KeyEvent_t xEvent;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    for(;;) {
        // Scan at 1kHz (every 1ms)
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1));
        
        // Perform matrix scan (non-blocking!)
        scanMatrix(&xEvent);
        
        // Queue the event (don't block!)
        if (xEvent.pressed != 255) {  // 255 = no event
            xEvent.timestamp = xTaskGetTickCount();
            if (xQueueSend(xKeyEventQueue, &xEvent, 0) != pdTRUE) {
                // Queue full - event dropped
                // This is OK for very fast playing
            }
        }
    }
}
```

### USB Send Task
```c
void vUSBSendTask(void *pvParameters) {
    USBSendItem_t xSendItem;
    TickType_t xTimeout = pdMS_TO_TICKS(10);  // 10ms max wait
    
    for(;;) {
        // Wait for data in queue
        if (xQueueReceive(xUSBSendQueue, &xSendItem, portMAX_DELAY) == pdTRUE) {
            // Try to send with timeout (CRITICAL!)
            TickType_t xStart = xTaskGetTickCount();
            
            // Non-blocking send attempt
            while (midi.sendPacket(xSendItem.data) == 0) {
                // Buffer full, wait a bit
                vTaskDelay(pdMS_TO_TICKS(1));
                
                // Timeout check
                if ((xTaskGetTickCount() - xStart) > xTimeout) {
                    // Give up - prevent indefinite blocking!
                    // Optionally: reset USB, drop message, etc.
                    break;
                }
            }
        }
    }
}
```

### Event Processing Task
```c
void vKeyEventTask(void *pvParameters) {
    KeyEvent_t xKeyEvent;
    USBSendItem_t xSendItem;
    
    for(;;) {
        // Get key event
        if (xQueueReceive(xKeyEventQueue, &xKeyEvent, portMAX_DELAY) == pdTRUE) {
            // Convert to MIDI
            uint8_t note = keyToNote(xKeyEvent.row, xKeyEvent.col);
            uint8_t status = xKeyEvent.pressed ? 0x90 : 0x80;
            
            // Build USB MIDI packet
            xSendItem.data[0] = 0x04;  // Cable 0, Code Index 4 (Note On/Off)
            xSendItem.data[1] = status;
            xSendItem.data[2] = note;
            xSendItem.data[3] = xKeyEvent.pressed ? 100 : 0;
            xSendItem.length = 4;
            
            // Queue for USB send (with timeout)
            if (xQueueSend(xUSBSendQueue, &xSendItem, pdMS_TO_TICKS(5)) != pdTRUE) {
                // Queue full - this indicates USB is way behind
                // Consider: reset USB, log error, etc.
            }
        }
    }
}
```

---

## Common FreeRTOS + USB Issues

### Issue 1: USB Interrupt Masked
**Symptom:** USB works initially, then stops responding
**Cause:** USB interrupt priority below configMAX_SYSCALL_INTERRUPT_PRIORITY
**Fix:** Set USB interrupt to higher priority (lower number)

### Issue 2: Stack Overflow in USB Task
**Symptom:** Random resets, corrupted data
**Cause:** USB stack uses more stack than allocated
**Fix:** Increase stack size, check with stack watermarks

### Issue 3: Queue Deadlock
**Symptom:** System hangs after extended use
**Cause:** Task waiting indefinitely on full queue
**Fix:** Always use timeouts on xQueueSend/xQueueReceive

### Issue 4: Priority Inversion
**Symptom:** Key presses delayed during USB activity
**Cause:** Low-priority task holding mutex needed by high-priority task
**Fix:** Use priority inheritance mutexes, minimize critical sections

### Issue 5: Heap Fragmentation
**Symptom:** System works initially, crashes after hours
**Cause:** Dynamic task creation/destruction fragments heap
**Fix:** Use static allocation, create all tasks at startup

---

## Watchdog Integration

### Independent Watchdog (IWDG) for USB Hangs
```c
void vUSBSendTask(void *pvParameters) {
    TickType_t xLastSuccessfulSend = xTaskGetTickCount();
    
    for(;;) {
        // ... send logic ...
        
        if (sendSuccessful) {
            xLastSuccessfulSend = xTaskGetTickCount();
            IWDG_ReloadCounter();  // Pet watchdog
        }
        
        // Check for USB hang
        if ((xTaskGetTickCount() - xLastSuccessfulSend) > pdMS_TO_TICKS(5000)) {
            // USB hasn't sent in 5 seconds - reset!
            NVIC_SystemReset();
        }
    }
}
```

---

## Recommendations Summary

1. **Use separate tasks** for scanning and USB sending
2. **Set USB interrupt priority high** (above configMAX_SYSCALL_INTERRUPT_PRIORITY)
3. **Use queues with timeouts** - never block indefinitely
4. **Allocate adequate stack** (512 words minimum for USB tasks)
5. **Implement watchdog** to recover from USB hangs
6. **Monitor queue depths** to detect backpressure
7. **Test with worst-case input** (all keys pressed rapidly)
