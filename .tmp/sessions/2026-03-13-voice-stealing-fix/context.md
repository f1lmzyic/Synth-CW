# Task Context: Voice Stealing Bug Fix - Indefinite Key Playback

Session ID: 2026-03-13-voice-stealing-fix
Created: 2026-03-13T00:00:00Z
Status: completed

## Current Request
Fix the bug causing indefinite key playback when 6+ keys are pressed simultaneously on the STM32 synth keyboard. The root cause is in the voice stealing logic in dsp.cpp that doesn't trigger proper release envelopes when stealing voices.

## Context Files (Standards to Follow)
- /Users/kavin/.config/opencode/context/core/standards/code-quality.md
- /Users/kavin/Documents/ES-synth-starter/.opencode/skills/agent-skill_stm32-freertos/SKILL.md
- /Users/kavin/Documents/ES-synth-starter/.opencode/skills/agent-skill_stm32-freertos/PATTERNS/INTERRUPT.md

## Reference Files (Source Material to Look At)
- /Users/kavin/Documents/ES-synth-starter/src/dsp.cpp (lines 240-280 - voice stealing logic)
- /Users/kavin/Documents/ES-synth-starter/include/globals.h (POLYPHONY=8, MAX_TOTAL_KEYS=36)
- /Users/kavin/Documents/ES-synth-starter/src/hw.cpp (key scanning)
- /Users/kavin/Documents/ES-synth-starter/src/main.cpp (message decoding)

## External Docs Fetched
N/A - This is a standalone synth (not MIDI), bug is in internal voice allocation logic

## Components
1. Voice allocation system (dsp.cpp:dspUpdateParams)
2. Envelope state machine (ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_IDLE)
3. Key-to-voice mapping (pianoKeyMap[], voiceKey[])

## Constraints
- POLYPHONY limited to 8 voices (hardware constraint)
- Must maintain real-time audio performance (22kHz sample rate)
- Cannot change data structures without breaking existing code
- Fix must be minimal and focused on the voice stealing bug

## Exit Criteria
- [ ] Voice stealing triggers proper release envelope (ENV_RELEASE state)
- [ ] Stolen voice envelope value preserved for smooth fade-out
- [ ] Code compiles without errors
- [ ] Fix verified to prevent indefinite note playback on 9+ key presses

## Bug Summary

**Root Cause**: In dsp.cpp:247-261, when voice stealing occurs:
1. `voiceEnvValue[freeVoice] = 0` - zeroes envelope (no release fade)
2. `voiceEnvState[freeVoice] = ENV_IDLE` - should be ENV_RELEASE
3. `pianoKeyMap[oldKey] = 0xFF` - cleared before release detection

**Result**: When the original key is released, release detection fails because pianoKeyMap was already cleared, causing indefinite playback.

**Fix**: Change voice stealing to trigger ENV_RELEASE state and preserve envelope value for proper fade-out.
