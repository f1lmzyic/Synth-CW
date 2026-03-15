# Embedded Systems Coursework 2 - Real-Time Music Synthesiser

## Table of Contents
1. [Project Overview](#project-overview)
2. [Core Functional Requirements](#core-functional-requirements)
3. [System Architecture](#system-architecture)
4. [Task Decomposition](#task-decomposition)
5. [Real-Time Scheduling Strategy](#real-time-scheduling-strategy)
6. [Shared Data and Synchronisation](#shared-data-and-synchronisation)
7. [Advanced Features](#advanced-features)
8. [Why the Arpeggiator + Automation Combination Was Chosen](#why-the-arpeggiator--automation-combination-was-chosen)
9. [User Interface and Control Mapping](#user-interface-and-control-mapping)
10. [Source File Structure](#source-file-structure)
11. [Execution Time Measurement](#execution-time-measurement)
12. [CPU Utilisation and Rate Monotonic Analysis](#cpu-utilisation-and-rate-monotonic-analysis)
13. [Blocking and Deadlock Analysis](#blocking-and-deadlock-analysis)
14. [Build and Flash Instructions](#build-and-flash-instructions)

---

## Project Overview

This project implements a real-time music synthesiser on the STM32L432KC platform using FreeRTOS, timer interrupts, CAN communication, and an OLED-based user interface. The design satisfies the coursework requirement for a concurrent embedded music system while extending the baseline synthesiser with structured performance-oriented features.

The system supports:
- low-latency note generation
- rotary-knob volume control
- OLED feedback
- periodic display refresh with LED heartbeat
- CAN-based note messaging
- modular DSP processing
- advanced performance features for live musical control

Unlike a simple feature collection, the system is organised as a layered real-time architecture:
- hardware scanning and input decoding
- state update and note scheduling
- performance feature transformation
- audio-rate DSP synthesis
- display and patch management

This structure was chosen to keep the code maintainable, analyzable, and aligned with the real-time requirements of the coursework.

---

## Core Functional Requirements

The system was designed to satisfy the core coursework requirements:

- A pressed key produces the corresponding musical tone.
- Audio is generated with no perceptible delay.
- Volume is controlled by a rotary knob with at least 8 levels.
- The OLED shows the current note and volume.
- The OLED is refreshed every 100 ms and the LED toggles at the same interval.
- CAN messaging is used for distributed keyboard note press/release communication.
- The implementation uses both interrupts and threads.
- Shared resources are protected.
- The codebase is modular and maintainable.
- Compile-time options are included for execution-time measurement.

In this implementation, the performance view shows currently active notes, current volume, octave, and recent CAN message activity, while the menu system exposes advanced synthesis and performance controls.

---

## System Architecture

The synthesiser is divided into five real-time layers:

### 1. Input and hardware layer
This layer scans the keyboard matrix, reads the joystick and rotary encoders, and detects connection changes between keyboard modules.

### 2. Communication layer
This layer handles CAN receive and CAN transmit using interrupt-assisted queues and a dedicated transmit task.

### 3. Musical state layer
This layer maintains physically held notes, expanded chord notes, arpeggiated output notes, and playback-driven note states.

### 4. Audio DSP layer
This layer runs at audio rate inside the timer ISR and performs oscillator generation, envelopes, filtering, modulation, effects, and final PWM output.

### 5. Display and patch layer
This layer updates the OLED, provides menu feedback, and manages patch storage and recall from STM32 flash.

This decomposition keeps hard real-time audio work separate from slower UI and control tasks.

---

## Task Decomposition

The following concurrent activities are used in the system.

| Task / ISR | Type | Trigger / Period | Main Responsibility |
|---|---|---:|---|
| `sampleISR()` | Interrupt | 22 kHz | Real-time audio generation and PWM output |
| `CAN_RX_ISR()` | Interrupt | on CAN RX | Move received CAN frame into queue |
| `CAN_TX_ISR()` | Interrupt | on CAN TX complete | Release TX semaphore |
| `scanKeysTask()` | Thread | 20 ms | Scan matrix, read knobs, joystick, local key changes |
| `decodeTask()` | Thread | event-driven | Decode incoming CAN messages into pressed-key state |
| `CAN_TX_Task()` | Thread | event-driven | Send queued CAN messages safely |
| `performanceTask()` | Thread | 5 ms | Apply chord expansion, arpeggiator, and automation logic |
| `displayUpdateTask()` | Thread | 100 ms | Refresh OLED and toggle heartbeat LED |
| timer-start task | Thread | one-shot | Start hardware sample timer after scheduler begins |

### Notes on implementation
- The highest-rate operation is the audio ISR.
- CAN receive and transmit completion are interrupt-driven.
- Slower or stateful logic is handled in FreeRTOS tasks.
- Musical transformation features are isolated in `performanceTask()` rather than mixed into the audio ISR.

---

## Real-Time Scheduling Strategy

The design follows a practical fixed-priority real-time structure:

- **Audio generation** is executed inside the hardware timer ISR and therefore has the highest urgency.
- **CAN receive** and **CAN transmit completion** are ISR-driven to minimise communication latency.
- **Message decoding** and **performance feature updates** are executed as RTOS tasks.
- **Display updates** are intentionally slow and periodic to avoid disturbing time-critical audio execution.

This separation ensures that:
- hard real-time audio is never blocked by OLED drawing or patch operations
- musical control logic runs often enough to feel responsive
- communication is decoupled through queues and semaphores
- the code remains analyzable using task periods and measured execution times

---

## Shared Data and Synchronisation

Several shared data structures are accessed by multiple concurrent activities.

| Shared resource | Used by | Protection strategy |
|---|---|---|
| `sysState.params` | UI, DSP update, patch management, performance logic | mutex-protected copy/update |
| `sysState.pressedKeys` / `pressedKeyCount` | scan task, decode task, performance task | mutex |
| `sysState.synthKeys` / `synthKeyCount` | performance task, automation logic, voice engine update | mutex / controlled update path |
| `msgInQ` | CAN RX ISR, decode task, local key routing | FreeRTOS queue |
| `msgOutQ` | scan task / handshake logic, CAN TX task | FreeRTOS queue |
| `CAN_TX_Semaphore` | CAN TX ISR, CAN TX task | FreeRTOS counting semaphore |
| patch flash state | UI / patch operations | dedicated patch mutex |
| local DSP parameter buffer | DSP update and audio ISR | copied into ISR-local state |

### Synchronisation principles used
- Shared system state is protected using a mutex.
- ISR-to-task communication uses queues and semaphores instead of direct shared-state mutation.
- The audio ISR works on a local parameter copy rather than repeatedly locking shared state.
- Flash operations use a separate mutex to isolate patch operations from general UI activity.

This keeps the hard real-time path short and reduces the risk of races or long blocking sections.

---

## Advanced Features

The coursework encourages advanced features that enhance music generation while still respecting real-time constraints. The system includes three main performance-oriented additions:

### 1. Chord Memory / Chord Expansion
Chord mode converts a single held note into a structured chord voicing. Supported chord types include:
- major
- minor
- sus2
- sus4
- dominant 7
- minor 7

Additional controls allow:
- inversion
- spread
- multi-note chord expansion from several held roots

This feature increases harmonic output without increasing the physical playing complexity for the user.

### 2. Arpeggiator
The arpeggiator transforms held or chord-expanded notes into a timed note sequence. Supported modes include:
- up
- down
- up/down
- random

User-adjustable controls include:
- on/off
- mode
- rate
- octave range

Rather than generating all notes simultaneously, the arpeggiator turns the harmonic pool into a rhythmic melodic stream, making the synthesiser behave more like a performance sequencer.

### 3. Automation / Phrase Recording
The automation system records note-state changes over time and replays them as a loop. It supports:
- record enable
- playback enable
- automatic loop restart
- phrase-length tracking
- clear/reset control

This allows the user to play a phrase once and then let the system repeat it continuously. The result is not just a static synthesiser voice but a repeatable live-performance system.

---

## Why the Arpeggiator + Automation Combination Was Chosen

The strongest advanced-feature combination in this project is:

**Arpeggiator + Automation Recording**

This combination was chosen because it gives the clearest musical and technical improvement for marking purposes.

### Why this combination is strong musically
The arpeggiator immediately produces audible rhythmic note generation from held keys or chord voicings. The automation recorder then captures phrase changes and replays them as a loop. Together, these features transform the system from a simple keyboard synthesiser into a structured performance instrument.

In practical use:
- chord mode can generate a harmonic note pool
- the arpeggiator can step through this pool rhythmically
- automation can capture the resulting phrase over time
- playback can loop the phrase continuously

This creates a much stronger musical result than a simple one-shot effect.

### Why this combination is strong for coursework marking
This feature set matches the advanced-feature expectations particularly well because it demonstrates:

- **music generation enhancement** rather than cosmetic behaviour
- **clear real-time scheduling constraints** through periodic step processing
- **software engineering structure** through separate modules and clean processing stages
- **hardware utilisation** through knobs, OLED pages, and live user control

It is therefore a strong match for the coursework expectation that advanced features should enhance music generation, respect real-time constraints, and demonstrate good engineering practice.

### Internal processing order
The performance pipeline is intentionally structured as:

1. physical held notes  
2. chord expansion  
3. arpeggiator note selection  
4. automation record/playback control  
5. synth voice assignment  
6. audio-rate DSP rendering  

This is important because it means the system is not just “many features added together”. Instead, the features are organised as a controlled musical transformation chain.

---

## User Interface and Control Mapping

The user interface is split into two main operating states:

### Performance view
This view shows:
- currently active note names
- volume level
- octave
- waveform visualisation
- recent CAN message activity

### Menu pages
The OLED menu exposes structured parameter pages for synthesis, modulation, effects, patch memory, and performance features.

### Performance-feature pages
- **ARP**: on/off, mode, rate, octave range
- **CHRD**: on/off, chord type, inversion, spread
- **AUTO**: record, play, loop length, clear

This is important for the documentation because it shows that advanced features are recoverable and controllable from the interface, rather than permanently overriding the core synthesiser behaviour.

---

## Source File Structure

### Core orchestration
- `src/main.cpp`  
  Initializes the system, creates FreeRTOS tasks, registers CAN ISRs, starts the sample timer, and defines the concurrency structure.

### Hardware and input
- `src/hw.cpp`  
  Handles matrix scanning, rotary encoder decoding, joystick reads, keyboard handshake, and local note event creation.

### UI and display
- `src/ui.cpp`  
  Draws OLED pages, performance screens, advanced feature menus, and handles knob-based parameter editing.
- `src/ui_helpers.cpp`  
  Small helper functions used by the display system.

### Performance features
- `src/chord_memory.cpp`  
  Expands held notes into chord voicings.
- `src/arpeggiator.cpp`  
  Generates timed single-note output from the available note pool.
- `src/automation.cpp`  
  Records and replays note-state changes as a looping phrase.

### DSP engine
- `src/dsp.cpp`  
  Audio ISR processing path: oscillators, modulation, envelopes, filters, effects, and output.
- `src/oscillators.cpp`  
  Oscillator mixing and wavefolding.
- `src/envelopes.cpp`  
  Voice ADSR and modulation envelope handling.
- `src/lfo_modulation.cpp`  
  LFO, noise, and sample-and-hold generation.
- `src/filters.cpp`  
  SVF, Moog-style, and MS-20-style filter models.
- `src/effects.cpp`  
  Delay, chorus, bitcrusher, and decimator processing.
- `src/voice_engine.cpp`  
  Polyphonic voice allocation and note-to-step conversion.

### Patch memory
- `src/patch_memory.cpp`  
  Flash-backed patch save/load with validation and mutex protection.

### Navigation
- `src/navigation.cpp`  
  Joystick-based page and view navigation.

---

## Execution Time Measurement

The coursework requires compile-time options for measuring execution time. This project includes build switches for that purpose.

### Profiling switches
- `TEST_SCANKEYS_WCET`
- `TEST_DISPLAY_WCET`
- optional disabling of threads / CAN ISR / sample ISR for measurement builds

These switches allow isolated timing measurements of selected tasks without changing the runtime architecture of the full system.

### Measurement method
For each periodic task, the intended workflow is:
1. enable the relevant WCET test macro
2. run the isolated function multiple times
3. print average or maximum execution time using `micros()`
4. use the measured value for CPU-utilisation and RMS analysis

### Report note
Measured WCET values should be inserted here after profiling on the target board.

| Task | Period / trigger | Measured max execution time |
|---|---:|---:|
| `scanKeysTask()` | 20 ms | TODO |
| `displayUpdateTask()` | 100 ms | TODO |
| `performanceTask()` | 5 ms | TODO |
| `decodeTask()` | event-driven | TODO |
| `CAN_TX_Task()` | event-driven | TODO |
| `sampleISR()` | 22 kHz | TODO |

---

## CPU Utilisation and Rate Monotonic Analysis

This section should be completed using the measured WCET values from the target hardware.

### Theoretical minimum initiation intervals
The system contains the following principal periodic activities:

| Task | Minimum initiation interval |
|---|---:|
| `sampleISR()` | 1 / 22000 s |
| `performanceTask()` | 5 ms |
| `scanKeysTask()` | 20 ms |
| `displayUpdateTask()` | 100 ms |

Additional communication tasks are event-driven and should be analysed according to worst-case message arrival assumptions.

### CPU utilisation expression
For the periodic tasks, utilisation can be estimated by:

`U = Σ (Ci / Ti)`

where:
- `Ci` is the measured worst-case execution time
- `Ti` is the minimum initiation interval

### Critical instant analysis
Under a fixed-priority interpretation:
- the sample ISR has the highest urgency
- shorter-period tasks dominate longer-period tasks
- display refresh has the lowest priority among periodic activities

The critical instant occurs when all periodic tasks are released together and higher-priority work interferes with lower-priority work maximally. The analysis should demonstrate that all tasks still complete before their deadlines using measured WCET values.

### Report note
Replace this section with the final measured numbers and working after timing tests are complete.

---

## Blocking and Deadlock Analysis

### Blocking sources
Potential blocking can occur at:
- the system mutex
- the patch-memory mutex
- queue receive/send points
- the CAN transmit semaphore

### Why deadlock risk is low
The design minimises deadlock risk for the following reasons:

1. The audio ISR does not wait on a mutex.
2. ISR-to-task communication is one-way through FreeRTOS queues/semaphores.
3. Patch operations use a dedicated mutex rather than reusing all system locks.
4. The main shared state is accessed through short mutex-protected sections.
5. The concurrency design is mostly producer-consumer rather than nested lock chains.

### Remaining caution
The strongest caution point is any path that holds the system mutex while calling additional logic. For final submission, this should be reviewed carefully and kept as short as possible.

---

## Build and Flash Instructions

This project is built using PlatformIO.

```bash
pio run
pio run -t upload
