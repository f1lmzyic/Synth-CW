# Embedded Systems Coursework 2 - Real-Time Music Synthesiser

## Table of Contents
1. [Project Overview](#project-overview)
2. [Coursework Requirements Coverage](#coursework-requirements-coverage)
3. [Key Technical Contributions](#key-technical-contributions)
4. [Performance Feature Set](#performance-feature-set)
5. [Synth Engine Feature Set](#synth-engine-feature-set)
6. [System Architecture](#system-architecture)
7. [Task Decomposition](#task-decomposition)
8. [Real-Time Scheduling Strategy](#real-time-scheduling-strategy)
9. [Shared Data and Synchronisation](#shared-data-and-synchronisation)
10. [User Interface and Control Mapping](#user-interface-and-control-mapping)
11. [Source File Structure](#source-file-structure)
12. [Execution Time Measurement](#execution-time-measurement)
13. [CPU Utilisation and Rate Monotonic Analysis](#cpu-utilisation-and-rate-monotonic-analysis)
14. [Blocking and Deadlock Analysis](#blocking-and-deadlock-analysis)
15. [Build and Flash Instructions](#build-and-flash-instructions)
16. [Hardware Specifications](#hardware-specifications)

---

## Project Overview

This project implements a real-time polyphonic music synthesiser on the STM32L432KC platform using FreeRTOS tasks, hardware timer interrupts, CAN communication, and an OLED-based user interface. The system was designed to satisfy the coursework requirements for concurrent embedded music generation while also extending the baseline synthesiser into a more performance-oriented instrument.

The project combines two equally important layers:

1. **Performance-oriented musical control**, centred around chord generation, arpeggiation, and automation recording.
2. **A complete modular synth engine**, including oscillators, modulation, filtering, effects, patch memory, and structured OLED control pages.

This balance is important because the project is not intended to be just a collection of isolated features. Instead, it is organised as a layered real-time system in which musical control logic, DSP processing, UI handling, and communication are cleanly separated and analysed.

---

## Coursework Requirements Coverage

The implementation was designed to address the core coursework requirements:

- note generation for pressed keys
- low-latency audio response
- rotary-knob volume control with multiple levels
- OLED feedback for note and system state
- periodic display refresh and LED heartbeat
- CAN-based note press/release communication
- use of both interrupts and threads
- protection of shared resources
- modular and maintainable code structure
- compile-time timing hooks for execution-time measurement

In addition to the baseline requirements, the project introduces advanced music-generation features that remain compatible with the real-time design of the system.

---

## Key Technical Contributions

The most distinctive aspect of this project is the way the advanced musical features are integrated into a structured processing pipeline rather than added as isolated effects.

The main musical contribution is the following three-stage performance feature set:

- **Chord Memory / Chord Expansion**
- **Arpeggiator**
- **Automation / Phrase Recording**

These three functions form the main creative layer of the instrument. They were selected because they produce a clear improvement in generated musical output while also being suitable for real-time scheduling analysis, shared-state handling, and hardware UI integration.

At the same time, the project also includes a complete synthesis engine with:

- dual-oscillator sound generation
- oscillator morphing and detune
- sub oscillator, noise, and ring modulation
- filter models and drive
- amplitude and modulation envelopes
- LFO and sample-and-hold modulation
- delay, chorus, bitcrushing, and decimation
- flash-based patch memory
- multiple OLED display modes

This makes the project both musically expressive and technically rich for coursework analysis.

---

## Performance Feature Set

The three advanced performance functions form the central musical identity of the system.

### 1. Chord Memory / Chord Expansion

Chord mode expands physically held notes into harmonic note sets before they are sent to the synth voice layer.

Supported controls include:

- chord enable/disable
- chord type selection
- inversion
- spread

This allows a single held root note to generate a fuller harmonic structure without increasing the physical playing complexity for the user.

### 2. Arpeggiator

The arpeggiator converts the currently available note pool into a rhythmic step sequence.

Supported controls include:

- arpeggiator enable/disable
- playback mode
- arpeggiator rate
- octave range

Rather than simply layering notes, this introduces time-structured melodic output and immediately makes the instrument more performance-oriented.

### 3. Automation / Phrase Recording

The automation system records and replays note-state activity over time as a looped phrase.

Supported controls include:

- record enable
- playback enable
- phrase length
- clear/reset

This allows the user to capture a musical phrase and replay it repeatedly, turning the synthesiser into a repeatable live-performance sequencer rather than only a direct-play keyboard.

### Why this feature set was prioritised

This combination was prioritised because it is especially strong from both a musical and coursework perspective.

Musically, it creates a clear progression:

1. held notes define a harmonic pool  
2. chord expansion enriches the note pool  
3. arpeggiation introduces rhythmic sequencing  
4. automation captures and replays the result over time  

From a coursework perspective, this is also a strong advanced-feature choice because it demonstrates:

- enhancement of music generation rather than cosmetic behaviour
- clear periodic and event-driven real-time behaviour
- structured state-machine style processing
- shared-resource management that can be documented cleanly
- visible use of knobs, OLED pages, and embedded hardware controls

For that reason, the advanced performance layer is the main distinguishing contribution of this project.

---

## Synth Engine Feature Set

Alongside the performance layer, the project contains a full modular-style synthesis engine. These features are also important because they provide the sound design depth that makes the performance features meaningful in practice.

### Sound Generation

The oscillator section supports:

- continuously morphable primary oscillator
- secondary oscillator with waveform selection
- oscillator detune
- oscillator sync
- sub oscillator
- white-noise component
- ring modulation
- wavefolding

This gives the instrument a broad range of tonal starting points, from simple subtractive textures to more aggressive digital timbres.

### Sound Shaping

The filter section supports:

- selectable filter type outputs
- multiple filter models
- filter resonance
- filter envelope depth
- pre-filter drive

The design includes different tonal filter responses rather than a single fixed filter stage, allowing the same note source to be shaped in different ways.

### Modulation and Envelopes

The modulation system includes:

- amplitude ADSR envelope
- modulation envelope
- LFO-based modulation
- sample-and-hold style modulation
- glide / portamento

This allows both standard subtractive behaviour and more animated parameter movement over time.

### Digital Effects

The effects chain includes:

- delay
- chorus
- bitcrusher
- decimator

These effects extend the character of the raw synthesis engine and increase the range of available textures without changing the underlying real-time structure of the project.

### Patch Memory and Preset Recall

The project also supports patch save/load behaviour using STM32 flash storage.

This allows:

- patch slot selection
- patch save
- patch load
- default patch restore

Patch memory is important for usability because it lets the instrument preserve complete synthesis configurations rather than requiring live reconfiguration after each power cycle.

### Display Modes

The OLED interface is not limited to parameter text. The project includes multiple display views for live interaction:

- **PERF** view for performance-oriented information
- **SCOPE** view for waveform visualisation
- **ENV** view for envelope visualisation

These views make the system easier to understand, test, and perform with.

---

## System Architecture

The project is organised as a layered real-time embedded system.

### 1. Input and hardware layer
This layer scans the keyboard matrix, reads the rotary encoders, reads the joystick, and detects multi-keyboard connection state.

### 2. Communication layer
This layer handles CAN message reception and transmission using ISR-triggered queue and semaphore interaction.

### 3. Musical state layer
This layer maintains physical key state, chord-expanded note state, arpeggiated note state, and automation playback state.

### 4. DSP and voice layer
This layer generates audio at sample rate using oscillator, envelope, modulation, filter, effects, and output stages.

### 5. UI and patch layer
This layer manages OLED rendering, page navigation, parameter editing, and patch memory interaction.

This decomposition helps keep the hard real-time path short while moving slower control and display logic into RTOS tasks.

---

## Task Decomposition

The following concurrent activities are used in the system.

| Task / ISR | Type | Trigger / Period | Main responsibility |
|---|---|---:|---|
| `sampleISR()` | Interrupt | 22 kHz | audio-rate synthesis and PWM output |
| `CAN_RX_ISR()` | Interrupt | on CAN RX | push incoming CAN frame into queue |
| `CAN_TX_ISR()` | Interrupt | on CAN TX complete | release transmit semaphore |
| `decodeTask()` | Thread | event-driven | decode queued CAN frames into note state |
| `CAN_TX_Task()` | Thread | event-driven | transmit outgoing CAN messages safely |
| `scanKeysTask()` | Thread | 20 ms | scan keyboard matrix, read knobs and local controls |
| `performanceTask()` | Thread | 5 ms | apply chord, arpeggiator, and automation logic |
| `displayUpdateTask()` | Thread | 100 ms | refresh OLED and toggle heartbeat LED |
| timer-start task | Thread | one-shot | start hardware sample timer after scheduler launch |

### Performance processing order

The performance path is intentionally structured in the following order:

1. physical held keys  
2. chord expansion  
3. arpeggiator output generation  
4. automation record/playback handling  
5. synth voice assignment  
6. audio-rate rendering  

This ordering is important because it shows that the advanced musical behaviour is implemented as a controlled processing chain rather than a set of unrelated additions.

---

## Real-Time Scheduling Strategy

The system follows a practical fixed-priority real-time design.

- Audio generation is handled in the timer ISR because it has the strictest timing requirement.
- CAN receive and CAN transmit completion are interrupt-driven to minimise latency.
- Input scanning, performance processing, and display updates are performed in FreeRTOS tasks.
- Slow UI drawing is isolated from hard real-time DSP execution.
- Shared state is copied or protected rather than accessed freely from all contexts.

This ensures that time-critical audio and communication behaviour are not blocked by OLED drawing or patch-management logic.

---

## Shared Data and Synchronisation

Several parts of the system share state across tasks and interrupts. These are protected using queues, semaphores, mutexes, and controlled copying.

| Shared resource | Used by | Protection strategy |
|---|---|---|
| `sysState.params` | UI, DSP update, patch logic, performance logic | mutex-protected access |
| `sysState.pressedKeys` / `pressedKeyCount` | scan, decode, performance | mutex |
| `sysState.synthKeys` / `synthKeyCount` | performance and voice update path | controlled update inside protected sections |
| `msgInQ` | CAN RX ISR, decode task | FreeRTOS queue |
| `msgOutQ` | scan/control logic, CAN TX task | FreeRTOS queue |
| `CAN_TX_Semaphore` | CAN TX ISR, CAN TX task | counting semaphore |
| patch flash storage | patch system and UI | dedicated patch mutex |
| local DSP parameter copy | DSP update path and audio ISR | copied into ISR-local state |

### Design rationale

The real-time path is kept safe by avoiding blocking operations inside the audio ISR. Slower stateful behaviour is handled by tasks, while ISR-to-task communication is done using queue/semaphore mechanisms rather than ad hoc shared-state mutation.

---

## User Interface and Control Mapping

The project uses both performance views and menu pages.

### Performance mode

In performance mode, the main controls provide quick access to live playing behaviour such as:

- master volume
- octave offset

The main display presents current system status in a performance-friendly format.

### Menu structure

The OLED menu exposes synthesis and performance parameters through structured pages:

- `OSC`
- `OSC2`
- `FLT`
- `MODEL`
- `ENV`
- `MOD`
- `MENV`
- `S&H`
- `FX`
- `CHO`
- `PATCH`
- `ARP`
- `CHRD`
- `AUTO`

This structure is important because it shows that the project is not only technically functional but also usable and recoverable in practice.

### Performance-feature pages

The three key advanced-function pages are:

- **ARP**: enable, mode, rate, octave range
- **CHRD**: enable, chord type, inversion, spread
- **AUTO**: record, play, length, clear

This gives the advanced feature set a clear and structured interface rather than hiding it behind hardcoded behaviour.

### Display views

The project also includes multiple display views:

- **PERF**
- **SCOPE**
- **ENV**

These views support both live use and debugging by making internal synthesis behaviour more visible.

---

## Source File Structure

### Core orchestration
- `src/main.cpp`  
  System setup, FreeRTOS task creation, ISR registration, timer startup, and top-level concurrency structure.

### Hardware and input
- `src/hw.cpp`  
  Hardware scanning, joystick reads, rotary encoder handling, and local note/control event detection.

### User interface
- `src/ui.cpp`  
  OLED rendering, page drawing, and knob-to-parameter mapping.
- `src/ui_helpers.cpp`  
  Helper functions for display rendering and waveform drawing.
- `src/navigation.cpp`  
  Joystick-based page and view navigation.

### Performance features
- `src/chord_memory.cpp`  
  Chord expansion of held keys into harmonic note pools.
- `src/arpeggiator.cpp`  
  Arpeggiated note generation from the available note pool.
- `src/automation.cpp`  
  Phrase recording, loop playback, and clear/reset operations.

### DSP engine
- `src/dsp.cpp`  
  Sample-rate processing path and overall audio rendering.
- `src/oscillators.cpp`  
  Waveform generation, oscillator mix, sub oscillator, ring modulation, and wavefolding.
- `src/envelopes.cpp`  
  Voice ADSR and modulation envelope handling.
- `src/lfo_modulation.cpp`  
  LFO, noise, and sample-and-hold generation.
- `src/filters.cpp`  
  Filter models and filter response processing.
- `src/effects.cpp`  
  Delay, chorus, bitcrushing, and decimation.
- `src/voice_engine.cpp`  
  Polyphonic voice allocation and pitch stepping.

### Patch memory
- `src/patch_memory.cpp`  
  Flash-backed patch save/load with validation and dedicated lock protection.

---

## Execution Time Measurement

The coursework requires timing analysis support. The codebase includes compile-time switches for isolated execution-time measurement.

### Profiling switches
- `TEST_SCANKEYS_WCET`
- `TEST_DISPLAY_WCET`
- optional disabling of threads, CAN ISR, and sample ISR for test builds

### Intended measurement workflow
1. enable the relevant timing macro  
2. run the selected task/function repeatedly  
3. collect average or maximum execution time  
4. use measured values for utilisation and schedulability analysis  

### Measurement table

| Task | Minimum initiation interval | Measured max execution time |
|---|---:|---:|
| `sampleISR()` | 1 / 22000 s | TODO |
| `performanceTask()` | 5 ms | TODO |
| `scanKeysTask()` | 20 ms | TODO |
| `displayUpdateTask()` | 100 ms | TODO |
| `decodeTask()` | event-driven | TODO |
| `CAN_TX_Task()` | event-driven | TODO |

---

## CPU Utilisation and Rate Monotonic Analysis

This section should be completed using measured worst-case execution times from the target hardware.

### Principal periodic activities

| Task | Minimum initiation interval |
|---|---:|
| `sampleISR()` | 1 / 22000 s |
| `performanceTask()` | 5 ms |
| `scanKeysTask()` | 20 ms |
| `displayUpdateTask()` | 100 ms |

### Utilisation model

CPU utilisation can be estimated using:

`U = Σ(Ci / Ti)`

where:
- `Ci` is the measured worst-case execution time
- `Ti` is the minimum initiation interval

### Critical instant discussion

Under a fixed-priority interpretation, the critical instant occurs when all periodic activities are released together and higher-priority execution interferes with lower-priority tasks maximally.

For this design, the analysis should treat:

- the audio ISR as the highest-urgency activity
- shorter-period tasks as higher-priority than longer-period tasks
- OLED refresh as a low-priority periodic task
- CAN tasks as event-driven interference sources

The final submission should replace this placeholder discussion with the measured numbers and a completed schedulability argument.

---

## Blocking and Deadlock Analysis

Potential blocking points exist at:

- the main system mutex
- the patch-memory mutex
- queue receive/send calls
- the CAN transmit semaphore

### Why deadlock risk is limited

The design reduces deadlock risk for several reasons:

1. the audio ISR does not block on a mutex  
2. ISR-to-task communication is queue/semaphore based  
3. patch memory uses a dedicated lock rather than sharing every lock path  
4. shared state is generally accessed in short, bounded sections  
5. the concurrency structure is mostly producer-consumer rather than nested lock chains  

### Caution point

The most important point to review before final submission is any path that may hold the system mutex while performing secondary logic. These sections should remain short and should not grow unnecessarily.

---

## Build and Flash Instructions

This project is built using PlatformIO.

```bash
pio run
pio run -t upload
