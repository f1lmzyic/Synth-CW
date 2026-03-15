# ES Monosynth Pro

This project transforms a basic STM32L432KC + FreeRTOS synthesizer skeleton into a feature-rich, dual-oscillator subtractive Monosynth. The architecture is modularized into discrete embedded subsystems (Hardware, DSP, UI) to provide professional-grade sound and stability.

## Features

Inspired by classic analog and digital synthesizers like the Moog Ladder, Korg MS-20, Roland SH-101, and ARP Odyssey, the DSP engine features:

### 1. Sound Generation (Oscillators & Noise)
*   **Dual Primary VCOs**:
    *   **OSC 1**: Features continuous **Waveform Morphing** (Sawtooth → Square → Triangle → Sine).
    *   **OSC 2**: Standard waveforms with adjustable **Detune** (-50 to +50 cents), **Octave Shift** (-2 to +2), and **Hard Sync** (OSC 1 phase resets OSC 2).
*   **Sub-Oscillator (SH-101 Style)**: A dedicated square wave that is permanently pitched exactly one octave below OSC 1. Adds massive bottom-end bass without tying up OSC 2.
*   **Noise Generator (White/Pink)**: Powered by a highly efficient 32-bit Linear Feedback Shift Register (LFSR). Essential for synthesizing percussive hits, wind, and lo-fi textures.
*   **Ring Modulation (AM)**: Multiplies the output of OSC 1 and OSC 2 to create complex, inharmonic, and bell-like metallic tones.

### 2. Sound Shaping (Filters, Drive, & Wavefolding)
*   **Three Distinct Filter Models**:
    *   **Standard SVF**: A clean, digital State-Variable Filter.
    *   **Moog Ladder Approximation (4-Pole LP)**: Features warm, non-linear feedback and soft clipping at the filter input for that signature classic "Moog" character.
    *   **MS-20 Sallen-Key Approximation**: Aggressive, screaming resonance with asymmetric clipping in the feedback path, perfect for acid basslines and gritty leads.
*   **Filter Types**: Selectable Low-Pass (LP), High-Pass (HP), Band-Pass (BP), and Notch.
*   **Pre-Filter Overdrive/Saturation**: A dedicated soft-clipping drive stage applied right before the filter block to harmonic saturation.
*   **Digital Wavefolding (Buchla/MicroFreak Style)**: Instead of clipping, loud signals fold backward upon themselves. Creates chaotic, bright, FM-like timbres when pushed hard.

### 3. Modulation & Envelopes
*   **Main VCA Envelope (ADSR)**: The primary Attack-Decay-Sustain-Release envelope tied to the final volume amplifier.
*   **Secondary Modulation Envelope (AD)**: A completely separate Attack-Decay envelope that can be routed with bipolar amounts (-64 to +64) to Global Pitch, Filter Cutoff, or OSC 2 Pitch.
*   **Multi-Wave LFO**: Global Low-Frequency Oscillator assignable to Pitch, Filter, or PWM.
*   **Sample & Hold (ARP 2600 Style)**: Samples the LFSR Noise Generator exactly at the start of every LFO cycle, creating classic stepped, randomized modulation. S&H output can be routed to Pitch or Filter Cutoff.
*   **Glide (Portamento)**: Slew limiter for sliding pitch between notes.

### 4. Digital Effects (FX)
*   **Digital Delay (Echo)**: An 8192-sample delay line with adjustable Time, Feedback, and Mix.
*   **Chorus / Ensemble (Juno-60 Style)**: A bucket-brigade style delay line (2048 samples) driven by a dedicated internal sine-wave LFO to thicken mono signals into wide stereo-like pads.
*   **Decimator (Sample Rate Reduction)**: Artificially drops the sample rate by holding the DSP output over several cycles, introducing aliasing and digital "ring".
*   **Bitcrusher**: Destructively shifts the 8-bit output down to as low as 1-bit resolution for extreme lo-fi crunch and digital distortion.

## Patch Memory System
*   **16 Patch Slots**: Store and recall complete synth presets in flash memory
*   **Patch Management Page**: Dedicated menu page for save/load/init operations
*   **Dirty Flag Indicator**: Visual feedback when current patch has unsaved changes

## Enhanced Display Modes
Long-press the joystick (1 second) to cycle through view modes:

*   **Performance View (PERF)**: Original view with note display, volume, and waveform visualization
*   **Oscilloscope View (SCOPE)**: Real-time waveform display with animated scope
*   **Envelope View (ENV)**: Visual ADSR envelope shape with parameter values

## User Interface & Navigation

The flat interface has been upgraded to a hierarchical, multi-page menu system navigated via the Joystick and Rotary Knob:

1.  **Short Press the Joystick**: Toggle between **Performance Mode** and **Menu Mode**
2.  **Long Press the Joystick** (1 sec): Cycle through view modes (PERF → SCOPE → ENV)
3.  **Move the Joystick Left/Right**: Switch between parameter pages.
4.  **Move the Joystick Up/Down**: Highlight a specific parameter on the active page
5.  **Turn the Rotary Knob**: Change the value of the highlighted parameter

### UI Menu Map
Navigate using the Joystick (L/R) and edit with the four rotary knobs:
*   `OSC`: Osc 1 Morph | Osc 2 Wave | Mix | Osc 2 Detune
*   `OSC2`: Sub Osc Mix | Noise Mix | Ring Mod Mix | Wavefolder
*   `FLT`: Cutoff | Resonance | Env Depth | Type (LP/HP/BP/Notch)
*   `MODEL`: Model (STD/MOOG/MS20) | Drive Level
*   `ENV`: Attack | Decay | Sustain | Release
*   `MOD`: LFO Rate | LFO Depth | *Empty* | Glide Time
*   `MENV`: Mod Env A | Mod Env D | Amount (+/-) | Target (Ptch/Flt/Osc2)
*   `S&H`: S&H Depth | S&H Target | *Empty* | *Empty*
*   `FX`: Delay Time | Delay Fbk | Delay Mix | Hard Sync Toggle
*   `CHO`: Chorus Rate | Chorus Depth | Chorus Mix | Bitcrusher Lvl
*   `PATCH`: Slot Select | Load | Save | Init

## System Architecture

The monolithic codebase has been restructured into a scalable C++ project:

*   **`src/main.cpp`**: Orchestrates FreeRTOS tasks and system initialization.
*   **`include/hw.h` / `src/hw.cpp`**: Hardware abstraction layer. Handles keyboard matrix scanning, joystick analog reads, and rotary encoder state machines within the 20ms `scanKeysTask`.
*   **`include/ui.h` / `src/ui.cpp` / `src/ui_helpers.cpp`**: Manages the OLED display drawing via U8g2 and processes rotary knob rotations to update the correct DSP parameters. Runs in the 100ms `displayUpdateTask`. Uses `ui_helpers.cpp` and `sine_lut.h` for waveform visualization.
*   **`include/dsp.h` / `src/dsp.cpp`**: The core sound engine. Runs entirely within the 22kHz `AudioISR` timer interrupt. Uses lock-free parameter buffering to ensure no audio dropouts occur while navigating the menu.
*   **`include/globals.h` / `include/constants.h`**: Defines the shared `sysState` structures, hardware pins, and concurrency primitives (Mutexes) used for IPC between tasks.
*   **`include/patch_memory.h` / `src/patch_memory.cpp`**: Patch memory management for storing/loading presets from STM32 flash memory. Handles CRC validation and thread-safe flash operations.

## Building and Flashing

This project is built using PlatformIO. To compile and upload to the Nucleo board:

```bash
pio run -t upload
```

Ensure the correct `lib_deps` are installed via `platformio.ini`:
*   `olikraus/U8g2`
*   `stm32duino/STM32duino FreeRTOS`

## Hardware Specifications

- **MCU**: STM32L432KC (ARM Cortex-M4)
- **Display**: SSD1305 128x32 OLED
- **Audio**: 22kHz sample rate, 8-bit resolution (PWM)
- **Key Matrix**: 3 rows x 4 columns (scanned)
- **Knobs**: Quadrature encoders via matrix
- **Joystick**: Analog X/Y and digital push button

## License

This project is an advanced extension of the Embedded Systems coursework synthesizer.