# Embedded Music Synthesizer

Real-time STM32 synthesizer with live control, OLED UI, and CAN-based multi-board support.

---

## Table of Contents

- [Overview](#overview)
- [Task Identification](#task-identification)
- [Task Characterization](#task-characterization)
  - [2.1 Minimum Initiation Intervals](#21-minimum-initiation-intervals)
  - [2.2 Worst Case Execution Time / CPU Utilization](#22-worst-case-execution-time--cpu-utilization)
- [CPU Utilisation](#cpu-utilisation)
- [Critical Instant Analysis](#critical-instant-analysis)
- [Shared Data Structures and Synchronisation](#shared-data-structures-and-synchronisation)
- [Deadlock Analysis](#deadlock-analysis)
- [Audio Pipeline](#audio-pipeline)
- [Controls and UI](#controls-and-ui)
  - [Display Pages](#display-pages)
- [Advanced Features](#advanced-features)

---

## Overview

This project implements a real-time 8-voice polyphonic music synthesizer on an STM32L432KC platform using FreeRTOS. The system handles note input, 22 kHz 8-bit PWM audio generation, OLED updates, and CAN communication using a mix of interrupts and FreeRTOS tasks.

The design separates time-critical audio work from slower interface and communication tasks. This makes the system easier to analyse and helps keep the audio path responsive. The synthesiser can be configured during compilation to act as a sender or receiver module, allowing up to 3 keyboards to be stacked via CAN bus.

---

## Demo Video

<!-- Add demo video here -->

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
| `decodeTask` | **25.2 ms** | `msgInQ` length is 36. Under worst-case CAN traffic, the queue can fill in (36 × 0.7 = 25.2) ms. |
| `CAN_TX_Task` | **60 ms** | `scanKeysTask` runs every 20 ms and can generate up to 12 outgoing messages each cycle, so a 36-item `msgOutQ` can fill in 60 ms. |

### 2.2 Worst Case Execution Time / CPU Utilization

The WCET values were measured separately by enabling the corresponding profiling `#define` one at a time. After collecting the timing result, CPU utilisation was calculated from the measured WCET and the minimum initiation interval. *(Note: CAN ISR times include the measured base loop overhead plus the ~4us and ~3us actual hardware ISR function overhead).*

| Task / ISR | Min (us) | Max / WCET (us) | Minimum initiation interval | CPU utilisation (%) |
|---|---:|---:|---:|---:|
| `sampleISR` | 23.00 | 34.00 | 45.45 us | 74.81 |
| `scanKeysTask` | 41.00 | 55.00 | 20000 us | 0.28 |
| `displayUpdateTask` | 14092.00 | 14135.00 | 100000 us | 14.14 |
| `CAN_RX_ISR` | 4.06 | 4.14 | 700 us | 0.59 |
| `CAN_TX_ISR` | 3.06 | 3.08 | 700 us | 0.44 |
| `decodeTask` | 7.00 | 10.00 | 36 exec / 25.2 ms | 1.43 |
| `CAN_TX_Task` | 6.00 | 10.00 | 36 exec / 60 ms | 0.60 |

Total CPU utilisation = 92.29%

---

## CPU Utilisation

CPU utilisation percentages are shown in the Task Characterization table above. Total utilisation is approximately 92%.

---

## Critical Instant Analysis

Under rate monotonic scheduling, the worst case happens when all periodic tasks and any related event-driven activities are released at the same time. In this system, the interrupt handlers run above the task level, so `sampleISR` always pre-empts the threads when needed. Among the tasks, `decodeTask` has the highest priority, followed by `scanKeysTask` and `CAN_TX_Task`, while `displayUpdateTask` has the lowest priority.

Using the measured WCET values, the total processor demand in this worst-case situation still stays within the available CPU time. The most timing-sensitive part is `sampleISR`, because it has to finish within 45.45 us, and the measured WCET is still below that limit. The other tasks run less often and also finish within their own minimum initiation intervals, even when interference from higher-priority work is included. From these results, all deadlines are still met under the critical instant assumption.

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
| **Voice allocation** | 8-voice polyphonic allocator with round-robin assignment, per-voice phase/ADSR state |
| **Oscillator section** | Generates the base sound using OSC1 (PolyBLEP morphing), OSC2 (with detune/hard-sync), and the sub-oscillator |
| **Additional sources** | Adds noise (32-bit LFSR) and ring modulation for more varied timbre |
| **Modulation** | Applies ADSR envelope (volume), AD envelope (modulation), LFO (pitch/filter targets), glide, and pitch bend |
| **Filter stage** | Three models: Standard SVF (LP/HP/BP/Notch), Moog Ladder (4-pole with soft-clip), and MS-20 Sallen-Key (asymmetric feedback) |
| **Nonlinear shaping** | Applies drive and wavefolding for stronger harmonic colouring |
| **Effects** | Adds delay (8192-sample), chorus (2048-sample BDD), bit-crusher, and decimator |
| **Output** | Scales and writes the final audio signal (22 kHz, 8-bit PWM) to the output path |
| **Patch Memory** | Save and load 16 presets to STM32 flash with CRC validation |

---

## Controls and UI

| Control Element | Purpose |
|---|---|
| **Keyboard matrix** | Used to enter notes directly on the board with no perceptible delay |
| **Rotary knobs** | Adjust the volume (at least 8 increments) or edit the currently selected parameter |
| **Joystick** | Handles mode changes, page movement, parameter selection, and display view changes |
| **OLED display** | Shows the performance screen (including current note and volume level), alternate views, and menu pages |
| **LED** | Toggles every 100ms alongside the OLED refresh |
| **Board connection logic** | Detects neighbouring boards and supports linked-board operation (West/Middle/East) via CAN bus |

### Interface Structure

The user interface uses a multi-page menu rather than a single flat screen. A short joystick press switches between performance mode and menu mode. A long press cycles through the available display views. Left and right movement changes page, while up and down movement selects a parameter on the current page. The highlighted parameter is then edited using the rotary knob. The UI tasks run every 100ms.

### Display Modes

- Performance view (shows the note being played and the current volume level)
- Oscilloscope view
- Envelope view

### Parameter Pages

- OSC page (Oscillator 1 wave morph, Oscillator 2 waveform, mix, detune)
- OSC2 page (Sub-oscillator, noise, ring modulation, wavefolder)
- FLT page (Filter cutoff, resonance, envelope depth)
- MODEL page (Filter type: SVF, Moog Ladder, MS-20)
- ENV page (ADSR envelope: attack, decay, sustain, release)
- MOD page (LFO rate/depth, glide time)
- MENV page (Modulation envelope parameters)
- S&H page (Sample and hold parameters)
- FX page (Delay time/feedback/mix, oscillator sync)
- CHO page (Chorus, bit-crusher, decimator parameters)
- PATCH page (Save/load presets)

---

## Advanced Features

| Feature | Description |
|---|---|
| **8-Voice Polyphony** | 8-voice polyphonic allocator with round-robin assignment |
| **Dual primary oscillators** | OSC1 provides continuous PolyBLEP waveform morphing (saw→square→tri→sine), while OSC2 adds standard waveforms with detune, octave shift, and hard sync |
| **Sub-oscillator and noise source** | A dedicated sub-oscillator reinforces the low end, and the 32-bit LFSR noise source is available for more percussive or textured sounds |
| **Multi-board CAN Stacking** | Configure as sender or receiver module (West/Middle/East). Shares voice allocation and key events across up to 3 stacked keyboards via CAN bus. |
| **Filter section** | Three models: Standard state-variable filter (LP, HP, BP, notch), Moog Ladder (4-pole with soft-clip), and MS-20 Sallen-Key (asymmetric feedback) |
| **Drive and wavefolding** | The signal can be shaped further using pre-filter drive and digital wavefolding |
| **Envelope and modulation control** | The synth includes ADSR envelope, LFO (rate/depth with pitch/filter targets), and glide/portamento |
| **Patch Memory** | Save and load 16 presets to STM32 flash memory, protected by CRC validation |
| **Integrated digital effects** | The output stage includes delay (8192-sample), chorus (2048-sample BDD), bit-crusher, and decimator |
