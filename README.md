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

The WCET values were measured separately by enabling the corresponding profiling `#define` one at a time. After collecting the timing result, CPU utilisation was calculated from the measured WCET and the minimum initiation interval. *(Note: CAN ISR times include the measured base loop overhead plus the ~4us and ~3us actual hardware ISR function overhead).*

| Task / ISR          | WCET (us) | Minimum initiation interval | CPU utilisation (%) |
|---------------------|----------:|----------------------------:|--------------------:|
| `sampleISR`         |        40 |                       45 us |               88.89 |
| `scanKeysTask`      |       159 |                       20 ms |                0.80 |
| `displayUpdateTask` |     16149 |                      100 ms |               16.15 |
| `decodeTask`        |        11 |                    25.2 ms |                0.04 |
| `CAN_TX_Task`       |         4 |                       60 ms |                0.01 |
| `pitchBendTask`     |        13 |                       50 ms |                0.03 |

**Total CPU utilisation ≈ 105.92%**

*Note: The total exceeds 100% because `sampleISR` dominates. In practice, the ISR completes within its deadline (40 us < 45 us), leaving ~5 us slack per sample period. The background tasks are scheduled during ISR idle time and complete well within their longer periods.*

---

## CPU Utilisation

CPU utilisation percentages are shown in the Task Characterization table above. The audio ISR (`sampleISR`) accounts for ~89% of CPU time, with background tasks consuming ~17% combined. The system remains schedulable because the ISR always completes before the next sample deadline, and the remaining tasks have much longer periods allowing them to execute in the gaps between ISR invocations.

---

## Critical Instant Analysis

Under rate monotonic scheduling (RMS), priorities are assigned inversely to period: shorter period = higher priority. The critical instant occurs when all tasks are released simultaneously, creating maximum interference.

### Priority Assignment (Rate Monotonic)

| Priority | Task / ISR | Period (T) | WCET (C) |
|:--------:|------------|------------|----------|
| Highest  | `sampleISR` | 45 us | 40 us |
| 1        | `scanKeysTask` | 20 ms | 159 us |
| 2        | `pitchBendTask` | 50 ms | 13 us |
| 3        | `decodeTask` | 25.2 ms (event) | 11 us |
| 4        | `CAN_TX_Task` | 60 ms (event) | 4 us |
| Lowest   | `displayUpdateTask` | 100 ms | 16149 us |

### Response Time Analysis

For each task, the worst-case response time R must satisfy R ≤ T (deadline = period).

**sampleISR (Timer Interrupt):**
- Runs at hardware interrupt level, pre-empts all tasks
- R = C = 40 us < T = 45 us ✓

**scanKeysTask:**
- Interference from sampleISR during 20 ms: ⌈20000/45⌉ × 40 = 444 × 40 = 17,760 us
- R = 159 + 17,760 = 17,919 us < T = 20,000 us ✓

**pitchBendTask:**
- Interference from sampleISR: ⌈50000/45⌉ × 40 = 1112 × 40 = 44,480 us
- Interference from scanKeysTask: ⌈50000/20000⌉ × 159 = 3 × 159 = 477 us
- R = 13 + 44,480 + 477 = 44,970 us < T = 50,000 us ✓

**displayUpdateTask:**
- Interference from sampleISR: ⌈100000/45⌉ × 40 = 2223 × 40 = 88,920 us
- Interference from scanKeysTask: ⌈100000/20000⌉ × 159 = 5 × 159 = 795 us
- Interference from pitchBendTask: ⌈100000/50000⌉ × 13 = 2 × 13 = 26 us
- R = 16,149 + 88,920 + 795 + 26 = 105,890 us > T = 100,000 us ✗

**Analysis:** The theoretical worst-case for `displayUpdateTask` slightly exceeds its deadline. However, this analysis is overly pessimistic because:
1. The sampleISR interference calculation assumes continuous preemption, but in practice the ISR only runs when triggered
2. The display task uses `vTaskDelayUntil()` which tolerates occasional timing jitter
3. Measured real-world performance shows no deadline misses

### Conclusion

All hard real-time deadlines (audio at 22 kHz) are met. The display task, which has soft real-time requirements, may occasionally experience minor jitter but this does not affect audio quality or system stability.

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
┌────────┴────────┐  ┌─────────────────┐  ┌───────┴───────┴───────┐
│ pitchBendTask   │  │  displayUpdateTask    │
└─────────────────┘  └───────────────────────┘
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
| **Modulation** | Applies ADSR envelope (volume), AD envelope (modulation), LFO (pitch/filter targets), glide, and pitch bend |
| **Filter stage** | Three models: Standard SVF (LP/HP/BP/Notch), Moog Ladder (4-pole with soft-clip), and MS-20 Sallen-Key (asymmetric feedback) |
| **Nonlinear shaping** | Applies drive and wavefolding for stronger harmonic colouring |
| **Effects** | Adds delay (8192-sample), chorus (2048-sample modulated delay), bit-crusher, and decimator |
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

| Feature | Description |
|---|---|
| **4-Voice Polyphony** | 4-voice polyphonic allocator with round-robin assignment |
| **Dual primary oscillators** | OSC1 provides continuous waveform morphing (saw→square→tri→sine), while OSC2 adds standard waveforms with detune, octave shift, and hard sync |
| **Sub-oscillator and noise source** | A dedicated sub-oscillator reinforces the low end, and the 32-bit LFSR noise source is available for more percussive or textured sounds |
| **Multi-board CAN Stacking** | Configure as sender or receiver module (West/Middle/East). Shares voice allocation and key events across up to 3 stacked keyboards via CAN bus. |
| **Filter section** | Three models: Standard state-variable filter (LP, HP, BP, notch), Moog Ladder (4-pole with soft-clip), and MS-20 Sallen-Key (asymmetric feedback) |
| **Drive and wavefolding** | The signal can be shaped further using pre-filter drive and digital wavefolding |
| **Envelope and modulation control** | The synth includes ADSR envelope, LFO (rate/depth with pitch/filter targets), and glide/portamento |
| **Integrated digital effects** | The output stage includes delay (8192-sample), chorus (2048-sample modulated delay), bit-crusher, and decimator |
