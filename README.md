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

This project implements a real-time 4-voice polyphonic music synthesizer on an STM32L432KC platform using FreeRTOS. The system handles note input, 22 kHz 8-bit PWM audio generation, OLED updates, and CAN communication using a mix of interrupts and FreeRTOS tasks.

The design separates time-critical audio work from slower interface and communication tasks. This makes the system easier to analyse and helps keep the audio path responsive. The synthesiser can be configured during compilation to act as a sender or receiver module, allowing up to 3 keyboards to be stacked via CAN bus.


---

## Demo Video

[![ES Monosynth Pro Demo](https://img.youtube.com/vi/55RAnd3v-z0/maxresdefault.jpg)](https://youtu.be/55RAnd3v-z0)

[Watch on YouTube](https://youtu.be/55RAnd3v-z0)

---

## Task Identification

| Task / ISR | Type | Trigger | Purpose |
|---|---|---|---|
| `sampleISR` | Timer interrupt | 22 kHz hardware timer | Audio generation: modulation, voice mix, filter/effects, output write |
| `scanKeysTask` | Thread | Periodic (20 ms) | Key scan, joystick read, knob decode, local control update |
| `decodeTask` | Thread | Event-driven (`msgInQ`) | Decode CAN messages, update board/key state |
| `CAN_TX_Task` | Thread | Event-driven (`msgOutQ` + `CAN_TX_Semaphore`) | Transmit queued CAN frames |
| `displayUpdateTask` | Thread | Periodic (100 ms) | Refresh OLED menu and performance views |
| `pitchBendTask` | Thread | Periodic (50 ms) | Process joystick Y-axis for pitch bend control |
| `CAN_RX_ISR` | Hardware interrupt | Event-driven | Push received CAN frame into `msgInQ` |
| `CAN_TX_ISR` | Hardware interrupt | Event-driven | Release TX mailbox via `CAN_TX_Semaphore` |

---

## Task Characterization

This section gives the minimum initiation interval for each task and ISR, together with the measured WCET and the resulting CPU utilisation.

### 2.1 Minimum Initiation Intervals

| Task / ISR         | Minimum initiation interval | Assumptions used |
|--------------------|----------------------------:|---|
| `sampleISR`        |                   **45 us** | Timer interrupt configured at 22 kHz (1/22000 = 45.45 us). |
| `scanKeysTask`     |                   **20 ms** | Periodic task using `vTaskDelayUntil()` with a 20 ms period. |
| `displayUpdateTask`|                  **100 ms** | Periodic task using `vTaskDelayUntil()` with a 100 ms period. |
| `decodeTask`       |                 **25.2 ms** | `msgInQ` length is 36. Under worst-case CAN traffic, the queue can fill in (36 × 0.7 = 25.2) ms. |
| `CAN_TX_Task`      |                   **60 ms** | `scanKeysTask` runs every 20 ms and can generate up to 12 outgoing messages each cycle, so a 36-item `msgOutQ` can fill in 60 ms. |
| `pitchBendTask`    |                   **50 ms** | Periodic task using `vTaskDelayUntil()` with a 50 ms period. |

### 2.2 Worst Case Execution Time / CPU Utilization

The WCET values were measured separately by enabling the corresponding profiling `#define` one at a time. After collecting the timing result, CPU utilisation was calculated from the measured WCET and the minimum initiation interval. *(Note: The `sampleISR` timing includes all processing from LFO through to PWM output. CAN ISRs are short and event-driven, with negligible CPU impact.)*

| Task / ISR          | WCET (us) | Minimum initiation interval | CPU utilisation (%) |
|---------------------|----------:|----------------------------:|--------------------:|
| `sampleISR`         |        33 |                       45 us |               73.33 |
| `scanKeysTask`      |       156 |                       20 ms |                0.78 |
| `displayUpdateTask` |     16416 |                      100 ms |               16.42 |
| `decodeTask`        |        11 |                    25.2 ms |                0.04 |
| `CAN_TX_Task`       |         4 |                       60 ms |                0.01 |
| `pitchBendTask`     |        11 |                       50 ms |                0.02 |

**Total CPU utilisation ≈ 90.60%**

*Note: The audio ISR (`sampleISR`) accounts for ~73% of CPU time. The ISR completes within its deadline (33 us < 45 us), leaving ~12 us slack per sample period. The background tasks consume ~17% combined and complete well within their longer periods.*

---

## CPU Utilisation

CPU utilisation percentages are shown in the Task Characterization table above. The audio ISR (`sampleISR`) accounts for ~73% of CPU time, with background tasks consuming ~17% combined. The total utilization of ~91% leaves sufficient headroom for the system to remain schedulable. The ISR always completes before the next sample deadline (33 us < 45 us), and the remaining tasks have much longer periods allowing them to execute in the gaps between ISR invocations.

---

## Critical Instant Analysis

Under rate monotonic scheduling (RMS), priorities are assigned inversely to period: shorter period = higher priority. The critical instant occurs when all tasks are released simultaneously, creating maximum interference.

### Priority Assignment (Rate Monotonic)

| Priority | Task / ISR | Period (T) | WCET (C) |
|:--------:|------------|------------|----------|
| Highest  | `sampleISR` | 45 us | 33 us |
| 1        | `scanKeysTask` | 20 ms | 156 us |
| 2        | `pitchBendTask` | 50 ms | 11 us |
| 3        | `decodeTask` | 25.2 ms (event) | 11 us |
| 4        | `CAN_TX_Task` | 60 ms (event) | 4 us |
| Lowest   | `displayUpdateTask` | 100 ms | 16416 us |

### Response Time Analysis

For each task, the worst-case response time R must satisfy R ≤ T (deadline = period).

**sampleISR (Timer Interrupt):**
- Runs at hardware interrupt level, pre-empts all tasks
- R = C = 33 us < T = 45 us ✓

**scanKeysTask:**
- Interference from sampleISR during 20 ms: ⌈20000/45⌉ × 33 = 445 × 33 = 14,685 us
- R = 156 + 14,685 = 14,841 us < T = 20,000 us ✓

**pitchBendTask:**
- Interference from sampleISR: ⌈50000/45⌉ × 33 = 1112 × 33 = 36,696 us
- Interference from scanKeysTask: ⌈50000/20000⌉ × 156 = 3 × 156 = 468 us
- R = 11 + 36,696 + 468 = 37,175 us < T = 50,000 us ✓

**displayUpdateTask:**
- Interference from sampleISR: ⌈100000/45⌉ × 33 = 2223 × 33 = 73,359 us
- Interference from scanKeysTask: ⌈100000/20000⌉ × 156 = 5 × 156 = 780 us
- Interference from pitchBendTask: ⌈100000/50000⌉ × 11 = 2 × 11 = 22 us
- R = 16,416 + 73,359 + 780 + 22 = 90,577 us < T = 100,000 us ✓

### Conclusion

All deadlines are met under worst-case conditions. The audio ISR completes well within its 45 μs deadline (33 μs). Background tasks also complete within their periods, even when accounting for interference from higher-priority work. The display task now meets its deadline with ~9.4 ms of slack time.

---

## Shared Data Structures and Synchronisation

| Shared resource | Accessed by | Protection method | Reason |
|---|---|---|---|
| `sysState` | UI tasks, audio/control logic, CAN decode path | `sysState.mutex` (FreeRTOS mutex) | Prevents inconsistent updates to shared synthesizer state |
| `sysState.pressedKeys[]` | `decodeTask` (write), `voiceEngineUpdateParams` (read) | `sysState.mutex` | Ensures atomic key press/release updates |
| `voices[]` | `sampleISR` (read), `voiceEngineUpdateParams` (write) | Lock-free design with `volatile` | ISR reads voice state; task writes with careful ordering (`active` flag set last) |
| `msgInQ` | `CAN_RX_ISR`, `decodeTask` | FreeRTOS queue | Safely transfers received CAN frames from ISR to task context |
| `msgOutQ` | producer tasks, `CAN_TX_Task` | FreeRTOS queue | Decouples message generation from CAN transmission |
| `CAN_TX_Semaphore` | `CAN_TX_Task`, `CAN_TX_ISR` | Counting semaphore | Synchronises task-level transmission with mailbox availability |
| `pitchBendValue` | `pitchBendTask` (write), `sampleISR` (read) | `__atomic_store_n` / `__atomic_load_n` | Lock-free atomic access for single-word value |
| `atomicKeyMask` | `scanKeysTask` (write), other tasks (read) | `std::atomic<uint32_t>` | Lock-free key state bitmask |

---

## Deadlock Analysis

Deadlock requires four conditions: mutual exclusion, hold-and-wait, no preemption, and circular wait. This analysis examines inter-task blocking dependencies.

### Resource Dependency Graph

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   CAN_RX_ISR    │────>│     msgInQ      │<────│   decodeTask    │
└─────────────────┘     └─────────────────┘     └────────┬────────┘
                                                         │
┌─────────────────┐     ┌─────────────────┐              │
│   CAN_TX_ISR    │────>│CAN_TX_Semaphore │<────┐        │
└─────────────────┘     └─────────────────┘     │        │
                                                │        ▼
┌─────────────────┐     ┌─────────────────┐     │  ┌─────────────────┐
│  scanKeysTask   │────>│     msgOutQ     │<────┼──│  CAN_TX_Task    │
└────────┬────────┘     └─────────────────┘     │  └─────────────────┘
         │                                      │
         │              ┌─────────────────┐     │
         └─────────────>│  sysState.mutex │<────┴──────────┐
                        └─────────────────┘                │
                              ▲     ▲                      │
         ┌────────────────────┘     └──────────────┐       │
         │                                         │       │
┌────────┴────────┐                        ┌───────┴───────┴───────┐
│ pitchBendTask   │                        │  displayUpdateTask    │
└─────────────────┘                        └───────────────────────┘
```

### Analysis by Deadlock Condition

**1. Mutual Exclusion:** Present - `sysState.mutex` provides exclusive access.

**2. Hold-and-Wait:** NOT present - No task holds one resource while waiting for another:
   - `decodeTask`: Takes mutex, does work, releases mutex. Never holds mutex while waiting on queue.
   - `CAN_TX_Task`: Waits on queue, then waits on semaphore. Never holds mutex.
   - `scanKeysTask`: Takes mutex briefly, releases before any blocking operation.
   - All UI tasks: Take mutex with timeout, release before blocking.

**3. No Preemption:** Mitigated - Mutex uses timeout (`pdMS_TO_TICKS(5)`) preventing indefinite blocking.

**4. Circular Wait:** NOT present - Only one mutex exists; no circular dependency possible.

### Blocking Scenarios

| Task | Blocks On | Maximum Block Time |
|------|-----------|-------------------|
| `decodeTask` | `msgInQ`, `sysState.mutex` | Queue: indefinite (by design), Mutex: 5ms timeout |
| `CAN_TX_Task` | `msgOutQ`, `CAN_TX_Semaphore` | Queue: indefinite, Semaphore: 10ms timeout |
| `scanKeysTask` | `sysState.mutex` | 5ms timeout |
| `pitchBendTask` | `sysState.mutex` | 5ms timeout |
| `displayUpdateTask` | `sysState.mutex` | 50ms timeout |

### Conclusion

**No deadlock is possible** in the current design because:
1. Single mutex (`sysState.mutex`) eliminates circular wait
2. ISRs never take mutexes (use ISR-safe queue/semaphore APIs)
3. All mutex acquisitions use timeouts, preventing indefinite blocking
4. No task holds a resource while waiting for another resource

---

## Audio Pipeline

| Stage | Function |
|---|---|
| **Voice allocation** | 4-voice polyphonic allocator with round-robin assignment, per-voice phase/ADSR state |
| **Oscillator section** | Generates the base sound using OSC1 (waveform morphing), OSC2 (with detune/hard-sync), and the sub-oscillator |
| **Additional sources** | Adds noise (32-bit LFSR) and ring modulation for more varied timbre |
| **Modulation** | Applies ADSR envelope (volume), LFO (filter targets), glide, and pitch bend |
| **Filter stage** | State Variable Filter (SVF) with selectable LP/HP/BP/Notch responses |
| **Nonlinear shaping** | Applies wavefolding for harmonic coloration |
| **Effects** | Adds delay (8192-sample with feedback and mix) |
| **Output** | Scales and writes the final audio signal (22 kHz, 8-bit PWM) to the output path |

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

- Performance view (shows the note being played, volume level, waveform preview, and octave)
- Oscilloscope view (real-time animated waveform display with key and pitch bend status)
- Envelope visualization (displayed within the ENV parameter page)

### Parameter Pages

- PERF page (Performance: waveform preview, octave control, volume)
- OSC page (Oscillator 1 wave morph, Oscillator 2 waveform, mix, detune)
- OSC2 page (Sub-oscillator, noise, ring modulation, wavefolder)
- FLT page (Filter cutoff, resonance, envelope depth, filter type LP/HP/BP/Notch)
- ENV page (ADSR envelope: attack, decay, sustain, release with visual display)
- MOD page (LFO rate/depth, glide time)
- FX page (Delay time/feedback/mix, oscillator sync)

---

## Advanced Features

This section highlights the key differentiating features that go beyond the basic requirements.

| Feature | Description |
|---|---|
| **4-Voice Polyphony** | 4-voice polyphonic allocator with round-robin assignment for richer chords |
| **Dual Oscillators** | OSC1 provides waveform morphing (saw→square→tri→sine), OSC2 adds detune, octave shift, and hard sync |
| **Sub-Oscillator** | Square wave one octave below for reinforced bass |
| **Noise Generator** | 32-bit LFSR noise for percussion and texture |
| **Ring Modulation** | Bright, inharmonic tones by multiplying OSC1 × OSC2 |
| **State Variable Filter** | SVF with selectable LP/HP/BP/Notch responses |
| **Wavefolding** | Threshold-based wavefolding for harmonic coloration |
| **ADSR Envelope** | Volume envelope with attack, decay, sustain, release stages |
| **LFO Modulation** | Low-frequency oscillator targeting filter cutoff |
| **Glide/Portamento** | Smooth pitch transitions between notes |
| **Pitch Bend** | Joystick-controlled pitch modulation |
| **Delay Effect** | 8192-sample delay with feedback and mix control |
| **Multi-Board CAN** | Stack up to 3 keyboards; sender/receiver modes with automatic voice sharing |
| **Real-Time Control** | Perceptible zero-latency response via 22 kHz audio ISR |
| **OLED Interface** | 128×32 display with waveform scope, envelope visualization, and 7 parameter pages |
| **Compile-Time Profiling** | `#define`-gated WCET measurement for each task ||
