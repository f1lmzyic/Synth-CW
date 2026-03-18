# Embedded Music Synthesizer

Real-time STM32 synthesizer with polyphony, live control, OLED UI, and CAN-based multi-board support.



---

## Overview

This project implements a real-time embedded music synthesizer on STM32. The system combines polyphonic sound generation, live parameter control, OLED feedback, and CAN communication between connected boards.

Rather than using a single main loop, the synthesizer is split into RTOS tasks and hardware interrupts. This keeps audio generation separate from display, control, and communication logic.

---

## Task Identification


| Task / ISR | Type | FreeRTOS Priority | Trigger | Purpose |
|---|---|---:|---|---|
| `sampleISR` | Timer interrupt | - | 22 kHz hardware timer | Audio generation: modulation, voice mix, filter/effects, output write |
| `scanKeysTask` | Thread | 2 | Periodic (20 ms) | Key scan, joystick read, knob decode, local control update |
| `decodeTask` | Thread | 3 | Event-driven (`msgInQ`) | Decode CAN messages, update board/key state |
| `CAN_TX_Task` | Thread | 2 | Event-driven (`msgOutQ` + `CAN_TX_Semaphore`) | Transmit queued CAN frames |
| `displayUpdateTask` | Thread | 1 | Periodic (100 ms) | Refresh OLED menu and performance views |
| `CAN_RX_ISR` | Hardware interrupt | - | Event-driven | Push received CAN frame into `msgInQ` |
| `CAN_TX_ISR` | Hardware interrupt | - | Event-driven | Release TX mailbox via `CAN_TX_Semaphore` |



## Task Characterization

This section outlines each task in terms of its theoretical minimum initiation interval and measured maximum execution time, in line with the coursework requirements.

### 2.1 Minimum Initiation Intervals


| Task / ISR | Minimum initiation interval | Assumptions used |
|---|---:|---|
| `sampleISR` | **45.45 us** | Timer interrupt configured at 22 kHz. |
| `scanKeysTask` | **20 ms** | Periodic task using `vTaskDelayUntil()` with a 20 ms period. |
| `displayUpdateTask` | **100 ms** | Periodic task using `vTaskDelayUntil()` with a 100 ms period. |
| `CAN_RX_ISR` | **0.7 ms** | Worst-case CAN traffic assumption: minimum CAN frame transmission interval taken as 0.7 ms. |
| `CAN_TX_ISR` | **0.7 ms** | One TX completion interrupt is produced per transmitted CAN frame, so the same minimum inter-arrival time is used. |
| `decodeTask` | **25.2 ms for 36 executions** | `msgInQ` length is 36. Under worst-case CAN traffic, the queue can fill in \(36 \times 0.7 = 25.2\) ms. |
| `CAN_TX_Task` | **60 ms for 36 executions** | `scanKeysTask` runs every 20 ms and can generate up to 12 outgoing messages each cycle, so a 36-item `msgOutQ` can fill in 60 ms. |

### 2.2 Measured Maximum Execution Time


| Task / ISR | Measured maximum execution time |
|---|---:|
| `sampleISR` | ___ us |
| `scanKeysTask` | ___ us |
| `displayUpdateTask` | ___ us |
| `CAN_RX_ISR` | ___ us |
| `CAN_TX_ISR` | ___ us |
| `decodeTask` | ___ us |
| `CAN_TX_Task` | ___ us |
