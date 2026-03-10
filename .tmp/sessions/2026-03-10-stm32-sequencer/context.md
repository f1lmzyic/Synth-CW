# Task Context: STM32 Sequencer with Drums & Snares

Session ID: 2026-03-10-stm32-sequencer
Created: 2026-03-10T00:00:00Z
Status: discovered

## Current Request
User wants to implement a sequencer with drums, snares, and other sounds to enhance the ES-synth-starter STM32 synthesizer project.

## Project Overview (ES-synth-starter)

**Hardware**: STM32L432KC (Nucleo L432KC)
- 32-bit ARM Cortex-M4F @ 80MHz
- 12-bit DAC audio output
- CAN bus for multi-node communication
- U8g2 OLED display support
- Key matrix scanning (up to 24+ keys)

**Current DSP Features**:
- Dual oscillators (saw, square, triangle, sine morphing)
- Sub oscillator & noise
- Ring modulator
- Wavefolder
- Multi-model filters: Moog Ladder, MS-20, SVF
- ADSR envelopes (amp + modulation)
- LFO with multiple targets
- Sample & Hold
- Delay line & Chorus effects
- Bitcrusher & Decimator

**Architecture**:
- FreeRTOS for task scheduling
- ISR-based audio generation (~22kHz)
- CAN bus for inter-node communication
- Rotary encoder + key matrix UI

---

## External Projects Discovered

### 1. Drumbox (spiricom/Drumbox)
- **URL**: https://github.com/spiricom/Drumbox
- **MCU**: STM32H7
- **Features**: Full drum machine firmware, 16-step sequencer, multiple drum sounds

### 2. Drumidy (EvgenyD/Drumidy)
- **URL**: https://github.com/EvgenyD/Drumidy
- **MCU**: STM32 Nucleo
- **Features**: MIDI drum module, pattern-based sequencing

### 3. STM32-drum (nguest/stm32-drum)
- **URL**: https://github.com/nguest/stm32-drum
- **MCU**: STM32F103
- **Features**: 12-bit resolution, SPI screen, pattern recording, tempo control

### 4. miosix-faust (andreaco/miosix-faust)
- **URL**: https://github.com/andreaco/miosix-faust
- **MCU**: STM32F407VG Discovery
- **Features**: Faust DSP integration, digital drum synth, MIDI input, rotary encoders

### 5. Electro-drums (spanceac/electro-drums)
- **URL**: https://github.com/spanceac/electro-drums
- **MCU**: STM32F100VET6B
- **Features**: 6 drum sounds (kick, snare, tom, crash, ride), 22kHz 16-bit samples, DAC output

### 6. BassMate (Duncan McIntyre)
- **URL**: https://github.com/hackster.io news article
- **MCU**: STM32F411 Black Pill
- **Features**: Drum machine + sequencer, NeoTrellis buttons, VS1053 DSP, rotary encoders

### 7. Drumboy & Synthgirl (Randomwaves)
- **URL**: https://www.randomwaves.io / Kickstarter
- **MCU**: STM32H7
- **Features**: 1/64 quantization, 8 bars, 8 measures, dual filters, effects processor

### 8. Drum Trigger 2040 (Adafruit)
- **URL**: https://learn.adafruit.com/16-step-drum-sequencer
- **Platform**: CircuitPython on QT Py RP2040
- **Features**: 16-step sequencer, 11 drum voices, MIDI output, General MIDI drum notes

---

## General MIDI Drum Notes (Standard 808/GM)
| Drum | Note # |
|------|--------|
| Bass Drum | 36 |
| Snare | 38 |
| Closed Hi-Hat | 42 |
| Open Hi-Hat | 46 |
| Tom Low | 41 |
| Tom Mid | 43 |
| Tom High | 45 |
| Crash | 49 |
| Ride | 51 |
| Clavinet | 37 |
| Cowbell | 56 |
| Clap | 39 |

---

## Implementation Ideas

### Option A: Sample-Based Drum Machine
- Store drum samples in flash/SPI flash
- Trigger playback via sequencer
- Mix with existing synth engine

### Option B: Synthesized Drums
- Use existing DSP engine with specific patches for kick/snare/hat
- Parameter automation for drum-like envelopes

### Option C: Hybrid Approach
- Sample-based for complex sounds (snare, clap, cymbals)
- Synthesized for kick (pitch envelope), hi-hats (noise + envelope)

### Sequencer Features to Implement
1. 16-step pattern grid
2. Multiple tracks (kick, snare, hihat, etc.)
3. Tempo control (BPM)
4. Pattern chaining
5. Swing/groove
6. Per-step velocity (optional)
7. Accent tracks

---

## Constraints & Notes
- STM32L432KC has 256KB flash, 64KB RAM
- Current audio ISR runs at ~22kHz
- FreeRTOS tasks available
- CAN bus could sync multiple boards

---

## Exit Criteria
- [ ] Sequencer plays drum patterns in time
- [ ] At least 4 drum tracks (kick, snare, hihat, other)
- [ ] Tempo adjustable via UI
- [ ] Pattern editable via keys/encoders
- [ ] Audio output mixes drums + existing synth
