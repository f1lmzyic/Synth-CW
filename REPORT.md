# Embedded Music Synthesizer

Real-time STM32 synthesizer with live control, OLED UI, and CAN-based multi-board support.

---

## Table of Contents

- [Overview](#overview)
- [Task Identification](#task-identification)
- [Task Characterization](#task-characterization)
  - [2.1 Minimum Initiation Intervals](#21-minimum-initiation-intervals)
  - [2.2 Worst Case Execution Time / CPU Utilization](#22-worst-case-execution-time--cpu-utilization)
- [CPU Utilisation by Task](#cpu-utilisation-percentage-plot)
- [Critical Instant Analysis](#critical-instant-analysis)
- [Shared Data Structures and Synchronisation](#shared-data-structures-and-synchronisation)
- [Deadlock Analysis](#deadlock-analysis)
- [Audio Pipeline](#audio-pipeline)
- [Controls and UI](#controls-and-ui)
  - [Display Pages](#display-pages)
- [Advanced Features](#advanced-features)

---

## Overview

This project implements a real-time music synthesizer on an STM32 platform. The system handles note input, audio generation, OLED updates, and CAN communication using a mix of interrupts and FreeRTOS tasks.

The design separates time-critical audio work from slower interface and communication tasks. This makes the system easier to analyse and helps keep the audio path responsive.

---
## Demo Video

The video below demonstrates the functionality of our board. It covers the basic operations, how the system behaves when multiple boards are connected, and several advanced features we added.










## <put the video here 












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

---

## Task Characterization

This section gives the minimum initiation interval for each task and ISR, together with the measured WCET and the resulting CPU utilisation.

### 2.1 Minimum Initiation Intervals

| Task / ISR | Minimum initiation interval | Assumptions used |
|---|---:|---|
| `sampleISR` | **45.45 us** | Timer interrupt configured at 22 kHz. |
| `scanKeysTask` | **20 ms** | Periodic task using `vTaskDelayUntil()` with a 20 ms period. |
| `displayUpdateTask` | **100 ms** | Periodic task using `vTaskDelayUntil()` with a 100 ms period. |
| `CAN_RX_ISR` | **0.7 ms** | Worst-case CAN traffic assumption: minimum CAN frame transmission interval taken as 0.7 ms. |
| `CAN_TX_ISR` | **0.7 ms** | One TX completion interrupt is produced per transmitted CAN frame, so the same minimum inter-arrival time is used. |
| `decodeTask` | **25.2 ms** | `msgInQ` length is 36. Under worst-case CAN traffic, the queue can fill in \(36 \times 0.7 = 25.2\) ms. |
| `CAN_TX_Task` | **60 ms** | `scanKeysTask` runs every 20 ms and can generate up to 12 outgoing messages each cycle, so a 36-item `msgOutQ` can fill in 60 ms. |

### 2.2 Worst Case Execution Time / CPU Utilization

The WCET values were measured separately by enabling the corresponding profiling `#define` one at a time. After collecting the timing result, CPU utilisation was calculated from the measured WCET and the minimum initiation interval.

| Task / ISR | WCET (us) | Minimum initiation interval | CPU utilisation (%) |
|---|---:|---:|---:|
| `sampleISR` | 22 | 45.45 us | 48.40 |
| `scanKeysTask` | 282 | 20000 us | 1.41 |
| `displayUpdateTask` | 16040 | 100000 us | 16.04 |
| `CAN_RX_ISR` | 3 | 700 us | 0.43 |
| `CAN_TX_ISR` | 1 | 700 us | 0.14 |
| `decodeTask` | 11 | 36 exec / 25.2 ms | 1.57 |
| `CAN_TX_Task` | 4 | 36 exec / 60 ms | 0.24 |

Total CPU utilisation = 68.23%

---

## CPU Utilisation Percentage Plot

<p align="center">
  <img src="cpu_utilisation.png" width="700">
</p>


---
## Critical Instant Analysis

Under rate monotonic scheduling, the worst case happens when all periodic tasks and any related event-driven activities are released at the same time. In this system, the interrupt handlers run above the task level, so `sampleISR` always pre-empts the threads when needed. Among the tasks, `decodeTask` has the highest priority, followed by `scanKeysTask` and `CAN_TX_Task`, while `displayUpdateTask` has the lowest priority.

Using the measured WCET values, the total processor demand in this worst-case situation still stays within the available CPU time. The most timing-sensitive part is `sampleISR`, because it has to finish within `45.45 us`, and the measured WCET is still below that limit. The other tasks run less often and also finish within their own minimum initiation intervals, even when interference from higher-priority work is included. From these results, all deadlines are still met under the critical instant assumption.


---


## Shared Data Structures and Synchronisation

| Shared resource | Accessed by | Protection method | Reason |
|---|---|---|---|
| `sysState` | UI tasks, audio/control logic, CAN decode path | `sysState.mutex` | Prevents inconsistent updates to shared synthesizer state |
| `msgInQ` | `CAN_RX_ISR`, `decodeTask` | FreeRTOS queue | Safely transfers received CAN frames from ISR to task context |
| `msgOutQ` | producer tasks, `CAN_TX_Task` | FreeRTOS queue | Decouples message generation from CAN transmission |
| `CAN_TX_Semaphore` | `CAN_TX_Task`, `CAN_TX_ISR` | Binary/counting semaphore | Synchronises task-level transmission with mailbox availability |
| `pitchBendValue` | input/control path, audio path | atomic/simple shared update | Small shared control value used without multi-step state changes |

---

## Deadlock Analysis

No deadlock path was found in the current design. The main shared state is protected by one mutex, `sysState.mutex`, rather than a chain of nested locks. This matters because deadlock usually needs a circular wait between multiple held resources.

The CAN path does not create that pattern. `CAN_RX_ISR` and `CAN_TX_ISR` do not take the mutex. Instead, they only post to the queue or semaphore using the ISR-safe FreeRTOS calls. `decodeTask` may use the shared state mutex while updating system state, but it does not wait on another lock at the same time. `CAN_TX_Task` waits for queue data and mailbox availability, but it does not hold `sysState.mutex` while doing so. Because of this, the design may experience short blocking, but not a true deadlock cycle.


---

## Audio Pipeline

| Stage | Function |
|---|---|
| **Voice allocation** | Assigns notes across available voices and reuses voices when required |
| **Oscillator section** | Generates the base sound using OSC1, OSC2, and the sub-oscillator |
| **Additional sources** | Adds noise and ring modulation for more varied timbre |
| **Modulation** | Applies ADSR, modulation envelope, LFO, sample-and-hold, glide, and pitch bend |
| **Filter stage** | Shapes the signal using selectable filter models, filter type, cutoff, and resonance |
| **Nonlinear shaping** | Applies drive and wavefolding for stronger harmonic colouring |
| **Effects** | Adds delay, chorus, decimation, and bitcrushing |
| **Output** | Scales and writes the final audio signal to the output path |
---

## Controls and UI

| Control Element | Purpose |
|---|---|
| **Keyboard matrix** | Used to enter notes directly on the board |
| **Rotary knobs** | Adjust the currently selected parameter |
| **Joystick** | Handles mode changes, page movement, parameter selection, and display view changes |
| **OLED display** | Shows the performance screen, alternate views, and menu pages |
| **Board connection logic** | Detects neighbouring boards and supports linked-board operation |

### Interface Structure

The user interface uses a multi-page menu rather than a single flat screen. A short joystick press switches between performance mode and menu mode. A long press cycles through the available display views. Left and right movement changes page, while up and down movement selects a parameter on the current page. The highlighted parameter is then edited using the rotary knob.

### Display Modes

- Performance view
- Oscilloscope view
- Envelope view

### Parameter Pages

- OSC page
- OSC2 page
- FLT page
- MODEL page
- ENV page
- MOD page
- MENV page
- S\&H page
- FX page
- CHO page

---

## Advanced Features

| Feature | Description |
|---|---|
| **Dual primary oscillators** | OSC1 provides continuous waveform morphing, while OSC2 adds standard waveforms with detune, octave shift, and hard sync |
| **Sub-oscillator and noise source** | A dedicated sub-oscillator reinforces the low end, and the noise source is available for more percussive or textured sounds |
| **Ring modulation** | OSC1 and OSC2 can be combined through ring modulation to produce brighter and more inharmonic tones |
| **Multiple filter models** | The filter section supports three selectable models together with LP, HP, BP, and notch responses |
| **Drive and wavefolding** | The signal can be shaped further using pre-filter drive and digital wavefolding |
| **Envelope and modulation control** | The synth includes ADSR, a separate AD modulation envelope, LFO, sample-and-hold, and glide |
| **Integrated digital effects** | The output stage includes delay, chorus, decimation, and bitcrushing |

