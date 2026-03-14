# Task Context: Multi-Keypress Polyphonic Implementation

Session ID: 2026-03-11-multi-keypress
Created: 2026-03-11
Status: in_progress

## Current Request
Implement multi-keypress handling that supports 12 keys on a single keyboard (no CAN) and scales to 36 keys when 3 keyboards are connected via CAN. Must use proper threading and play all keys properly with polyphony.

## Context Files (Standards to Follow)
- Code quality standards from opencode context (maintain existing style)
- Existing code patterns in the project

## Reference Files (Source Material)
- src/hw.cpp - Key matrix scanning (existing)
- src/main.cpp - Main application, task setup, CAN handling
- src/dsp.cpp - Audio generation and synthesis engine
- include/constants.h - Pin definitions, mode configuration
- include/globals.h - SystemState, SynthParams structures

## External Docs Fetched
- ES-synth2 multi-keypress implementation (from ExternalScout):
  - 8-voice polyphony with phase accumulators
  - Dynamic voice allocation
  - PianoKeyMap[84] for reverse lookup
  - CAN message protocol with keyboard ID

## Components
1. **Multi-Key State** - Track all pressed keys (array, not single int)
2. **Voice Allocation** - 8 phase accumulators with dynamic assignment
3. **Key-to-Voice Mapping** - Reverse lookup to prevent duplicate notes
4. **Thread Architecture** - scanKeysTask → decodeTask → voiceAllocTask → sampleISR

## Constraints
- Maintain existing functionality (menu, knobs, joystick, CAN, DSP effects)
- Keep code quality consistent with existing style
- Support 12 keys single keyboard, 36 keys with 3 CAN-connected keyboards
- 8-voice polyphony default

## Exit Criteria
- [ ] Multiple keys can be pressed and held simultaneously
- [ ] Each pressed key produces sound (polyphonic)
- [ ] Releasing any key stops only that note
- [ ] No existing functionality broken (menu, knobs, CAN, effects)
- [ ] Clean compilation with no errors
