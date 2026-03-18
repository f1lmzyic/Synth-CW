# Embedded Music Synthesizer

Real-time STM32 synthesizer with polyphony, live control, OLED UI, and CAN-based multi-board support.



---

## Overview

This project implements a real-time embedded music synthesizer on STM32. The system combines polyphonic sound generation, live parameter control, OLED feedback, and CAN communication between connected boards.

Rather than using a single main loop, the synthesizer is split into RTOS tasks and hardware interrupts. This keeps audio generation separate from display, control, and communication logic.

---

## Task Identification

The final system combines FreeRTOS threads with hardware interrupts.  
The table below identifies the active execution contexts used in the synthesizer and summarises their role in the implemented codebase.

| Task / ISR | Type | FreeRTOS Priority | Trigger | Purpose |
|---|---|---:|---|---|
| `sampleISR` | Timer interrupt | - | 22 kHz hardware timer | Runs the audio path once per sample: updates modulation state, calls `voiceEngineUpdateParams()`, mixes active voices, applies filter/effects processing, and writes the final output sample. |
| `scanKeysTask` | Thread | 2 | Periodic (20 ms) | Scans the keyboard matrix, reads joystick state, interprets knob movement, updates local control state, and pushes note/control messages into `msgOutQ` when CAN transmission is needed. |
| `decodeTask` | Thread | 3 | Event-driven (`msgInQ`) | Pulls received frames from `msgInQ`, decodes handshake/key messages, updates `keyboardId` / `mainKeyboardId`, and maintains the shared `pressedKeys` state used by the voice engine. |
| `CAN_TX_Task` | Thread | 2 | Event-driven (`msgOutQ` + `CAN_TX_Semaphore`) | Pops outgoing frames from `msgOutQ` and sends them through the CAN peripheral when `CAN_TX_Semaphore` indicates that a transmit mailbox is free. |
| `displayUpdateTask` | Thread | 1 | Periodic (100 ms) | Refreshes the OLED by rendering the current UI page, including menu screens, performance view, scope-style display, and envelope visualisation. |
| `CAN_RX_ISR` | Hardware interrupt | - | Event-driven | Receives a CAN frame from hardware FIFO and appends it to `msgInQ` using the ISR-safe queue path. |
| `CAN_TX_ISR` | Hardware interrupt | - | Event-driven | Signals CAN transmit completion and gives back mailbox availability through `CAN_TX_Semaphore`, allowing `CAN_TX_Task` to continue sending queued frames. |
