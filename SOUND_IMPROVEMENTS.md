# Synth Sound Quality Improvement Guide

## Key Issues Fixed

### 1. **Proper PolyBLEP Implementation**
The previous PolyBLEP was broken - it used a simple lookup table which doesn't actually reduce aliasing. The correct implementation:
- Calculates the fractional position within the sample period (`t`)
- Calculates the phase increment as a fraction (`dt`)
- Applies the polynomial correction only at discontinuities
- Properly scales the correction based on waveform type

### 2. **Gain Staging**
Critical for good sound. Each stage should have appropriate levels:
- **Oscillators**: ±127 range
- **Mixer**: Soft clip at ±127 to prevent harsh digital clipping
- **Filter**: Handle ±256 internally for headroom
- **VCA**: Proper envelope scaling (0-255)
- **Master**: Final attenuation to prevent clipping

### 3. **Filter Improvements**
- Fixed coefficient calculations
- Added proper input headroom (±256 instead of ±127)
- Better resonance feedback scaling
- Proper state variable filter implementation

### 4. **Envelope Fixes**
- Correct attack/decay/sustain/release scaling
- Proper envelope value ranges (Q8.8 fixed point)
- Smooth transitions between envelope stages

### 5. **DC Blocking Filter**
- Simple first-order highpass at output
- Removes DC offset that causes clicks and speaker damage
- Coefficient: 0.996 (~10Hz cutoff at 22kHz sample rate)

## What Makes a Synth Sound "Proper"

### Essential Characteristics

1. **In-Tune Oscillators**
   - Accurate step size calculations
   - Stable phase accumulation
   - Proper detuning for thickness (5-15 cents)

2. **Band-Limited Waveforms**
   - PolyBLEP for saw/square waves
   - Reduces harsh high-frequency aliasing
   - Especially important at 22kHz sample rate

3. **Musical Filter**
   - Smooth cutoff tracking
   - Controllable resonance without instability
   - Envelope modulation depth

4. **Natural Envelopes**
   - Fast attack (0-20ms)
   - Musical decay times
   - Proper sustain levels
   - Clean release without clicks

5. **Good Gain Structure**
   - No unexpected clipping
   - Consistent output level
   - Headroom at each stage

6. **Oscillator Detune**
   - Slight detune (5-10 cents) creates thickness
   - OSC2 at same octave with detune = classic mono synth sound
   - Sub oscillator adds bass weight

## Recommended Starting Patches

### Fat Lead
```
OSC1: Saw (morph=0)
OSC2: Saw, Detune=+7, Mix=30%
Sub: 20%
Filter: Cutoff=100, Res=30, EnvDepth=50
Envelope: A=5, D=30, S=80, R=20
```

### Bass
```
OSC1: Saw→Square morph=64
OSC2: Square, Detune=-5, Octave=-1, Mix=40%
Sub: 40%
Filter: Cutoff=60, Res=40, EnvDepth=70
Envelope: A=5, D=20, S=60, R=15
```

### Pad
```
OSC1: Triangle (morph=192)
OSC2: Sine, Detune=+10, Mix=50%
Sub: 0%
Filter: Cutoff=80, Res=20, EnvDepth=30
Envelope: A=20, D=40, S=100, R=40
Chorus: Rate=20, Depth=30, Mix=40
```

## Testing Checklist

- [ ] All waveforms sound clean (no harsh aliasing)
- [ ] Filter sweeps are smooth (no zipper noise)
- [ ] Envelopes trigger cleanly (no clicks)
- [ ] Detune creates thickness, not dissonance
- [ ] Master volume doesn't clip
- [ ] Effects (delay/chorus) don't cause distortion
- [ ] Polyphony handling (mono with proper retrigger)

## Further Improvements

1. **Oversampling** (4x or 8x) - Move aliasing above audible range
2. **Proper Anti-Aliasing Filter** - Steep lowpass at Nyquist
3. **Wavetable Oscillators** - Multiple cycles per waveform
4. **Filter Key Tracking** - Higher notes = brighter filter
5. **Velocity Sensitivity** - If keyboard supports it
6. **Unison Mode** - Stack 3+ voices with detune
7. **Better Noise Colors** - Pink, brown for different textures
