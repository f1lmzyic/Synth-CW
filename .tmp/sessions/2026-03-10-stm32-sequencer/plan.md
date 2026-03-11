---
status: in-progress
phase: 1
updated: 2026-03-10
---

# Implementation Plan: STM32 Drum Sequencer

## Goal
Add a 16-step drum sequencer with kick, snare, hi-hat, and additional drums to the ES-synth-starter, using synthesized drum sounds via the existing DSP engine.

## Context & Decisions
| Decision | Rationale | Source |
|----------|-----------|--------|
| Synthesized drums over samples | Limited flash (256KB), allows real-time parameter tweaking | `ref:project-constraints` |
| 16-step pattern grid | Industry standard, fits 4/4 timingref:external-research | `` |
| 4 tracks minimum (kick, snare, hat, other) | Covers basic drum kit | `ref:external-research` |
| BPM-based timing with FreeRTOS timer | Precise timing, doesn't block audio ISR | `ref:existing-architecture` |
| Use existing DSP engine | Leverages wavefolder, filters, envelopes already implemented | `ref:existing-dsp` |

## Phase 1: Core Sequencer Infrastructure [IN PROGRESS]
- [ ] **1.1 Create sequencer data structures** ← CURRENT
  - Pattern storage (16 steps × 8 tracks)
  - Track configuration (sound type, parameters)
  - Sequencer state machine
- [ ] 1.2 Implement timing engine
  - BPM to microseconds conversion
  - Step advancement logic
  - Swing/groove option
- [ ] 1.3 Create sequencer task (FreeRTOS)
  - Task that advances steps on beat
  - Queue events to audio ISR

## Phase 2: Drum Sound Synthesis [PENDING]
- [ ] 2.1 Kick drum synthesis
  - Pitch envelope (freq sweep down)
  - Short decay, noise layer
- [ ] 2.2 Snare drum synthesis
  - Noise + tone mix
  - Snappy attack, medium decay
- [ ] 2.3 Hi-hat/cymbal synthesis
  - Filtered noise
  - Short decays, different freq cuts
- [ ] 2.4 Additional drums (tom, clap, cowbell)
  - Parameterized variations

## Phase 3: UI Integration [PENDING]
- [ ] 3.1 Display sequencer state
  - Show active step
  - Pattern visualization
- [ ] 3.2 Key/encoder input
  - Step toggle on key press
  - Track selection
  - Tempo adjustment
- [ ] 3.3 Pattern management
  - Save/load patterns
  - Pattern chaining

## Phase 4: Audio Integration [PENDING]
- [ ] 4.1 Mix drums with synth
  - Drum output to main mixer
  - Volume per track
- [ ] 4.2 Trigger logic
  - Note-on/off per step
  - Velocity support (optional)

## Phase 5: Testing & Refinement [PENDING]
- [ ] 5.1 Timing accuracy test
- [ ] 5.2 Sound quality check
- [ ] 5.3 UI usability test

## Notes
- 2026-03-10: Plan created based on external research from Drumbox, Drumidy, miosix-faust projects
- 2026-03-10: Using synthesized drums to maximize flash efficiency on STM32L432KC
- 2026-03-10: Will integrate with existing DSP engine to reuse filters, envelopes, wavefolder
