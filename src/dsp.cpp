#include "dsp.h"
#include <Arduino.h>
#include "constants.h"
#include "sine_lut.h"

const char* waveformNames[] = {"Saw", "Sqr", "Tri", "Sin", "-"};

// Local copy of params for lock-free ISR access
static SynthParams localParams;
static volatile uint32_t currentStepSize = 0;
static volatile uint32_t targetStepSize = 0;

// DSP state
static uint32_t phase1 = 0;
static uint32_t phase2 = 0;
static uint32_t lfoPhase = 0;

// Envelope state
static uint32_t envPhase = 0;
static int32_t envValue = 0; // 0 to 255<<8
enum EnvState { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
static EnvState envState = ENV_IDLE;

// Mod Envelope state
static int32_t modEnvValue = 0;
static EnvState modEnvState = ENV_IDLE;

// Sample & Hold state
static int32_t shValue = 0;
static uint32_t lastLfoPhase = 0;

// Noise LFSR state
static uint32_t lfsrState = 0xACE1u;

// Bitcrusher/Decimator state
static int32_t lastDecimatorVal = 0;
static uint8_t decimatorCounter = 0;

// Chorus Line
#define CHORUS_BUFFER_SIZE 2048
static int16_t chorusBuffer[CHORUS_BUFFER_SIZE];
static uint32_t chorusWritePos = 0;
static uint32_t chorusLfoPhase = 0;

// Delay Line
#define DELAY_BUFFER_SIZE 8192
static int16_t delayBuffer[DELAY_BUFFER_SIZE];
static uint32_t delayWritePos = 0;

// Filter state
static int32_t f_lp = 0;
static int32_t f_bp = 0;

// Moog filter state
static int32_t moog_s[4] = {0, 0, 0, 0};

// MS-20 filter state
static int32_t ms20_s[2] = {0, 0};

// Note frequencies - calibrated for 22kHz sample rate
const uint32_t baseStepSizes[] = {51076057, 54113183, 57330935, 60740010, 64351798,
                                  68178355, 72232452, 76527617, 81078186, 85899345,
                                  91007186, 96418755};

// ============================================================================
// Parameter smoothing (prevents zipper noise)
// ============================================================================
static int32_t smoothCutoff = 255;
static int32_t smoothVol = 4;

void dspInit() {
    memset(delayBuffer, 0, sizeof(delayBuffer));
    memset(chorusBuffer, 0, sizeof(chorusBuffer));

    // Default params - starting with a good basic sound
    localParams.osc1WaveMorph = 0;      // Sawtooth
    localParams.osc2Wave = WAVEFORM_SAWTOOTH;
    localParams.osc2Detune = 5;         // Slight detune for thickness
    localParams.osc2Octave = 0;         // Same octave
    localParams.mixOsc2 = 30;           // More OSC1, less OSC2

    localParams.subOscMix = 20;         // Add some sub bass
    localParams.noiseMix = 0;
    localParams.ringModMix = 0;

    localParams.filterCutoff = 100;     // Start with open filter
    localParams.filterRes = 30;         // Some resonance
    localParams.filterEnvDepth = 50;    // Envelope to filter
    localParams.filterType = 0;         // Lowpass
    localParams.filterModel = 0;        // Standard SVF
    localParams.filterDrive = 0;
    localParams.wavefold = 0;
    localParams.oscSync = false;

    localParams.envAttack = 5;
    localParams.envDecay = 30;
    localParams.envSustain = 80;
    localParams.envRelease = 20;

    localParams.modEnvAttack = 10;
    localParams.modEnvDecay = 40;
    localParams.modEnvAmount = 0;
    localParams.modEnvTarget = 0;

    localParams.lfoRate = 20;
    localParams.lfoDepth = 0;
    localParams.lfoTarget = 0;
    localParams.shDepth = 0;
    localParams.shTarget = 0;

    localParams.glideTime = 0;

    localParams.delayTime = 0;
    localParams.delayFeedback = 0;
    localParams.delayMix = 0;

    localParams.chorusRate = 0;
    localParams.chorusDepth = 0;
    localParams.chorusMix = 0;

    localParams.bitcrushDepth = 0;
    localParams.decimatorRate = 0;

    localParams.masterVol = 6;          // Good output level

    smoothCutoff = localParams.filterCutoff;
    smoothVol = localParams.masterVol;

    if (sysState.mutex != NULL) {
        if (xSemaphoreTake(sysState.mutex, portMAX_DELAY) == pdTRUE) {
            sysState.params = localParams;
            xSemaphoreGive(sysState.mutex);
        }
    }
}

// Calculate step size for a given MIDI note
static uint32_t getStepSizeForMidiNote(int note) {
    if (note < 0) return 0;

    int noteIndex = note % 12;
    int octave = (note / 12) - 5;

    uint32_t stepSize = baseStepSizes[noteIndex];

    if (octave > 0) {
        stepSize <<= octave;
    } else if (octave < 0) {
        stepSize >>= (-octave);
    }
    return stepSize;
}

void dspUpdateParams() {
    if (sysState.mutex != NULL) {
        if (xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            localParams = sysState.params;

            int key = sysState.pressedKey;

            if (key >= 0) {
                targetStepSize = getStepSizeForMidiNote(key);
                if (envState == ENV_IDLE || envState == ENV_RELEASE) {
                    envState = ENV_ATTACK;
                    modEnvState = ENV_ATTACK;
                    if (localParams.glideTime == 0) {
                        currentStepSize = targetStepSize;
                    }
                }
            } else {
                if (envState != ENV_IDLE) {
                    envState = ENV_RELEASE;
                }
                if (modEnvState != ENV_IDLE) {
                    modEnvState = ENV_RELEASE;
                }
            }

            smoothCutoff = localParams.filterCutoff;
            smoothVol = localParams.masterVol;

            xSemaphoreGive(sysState.mutex);
        }
    }
}

// Generate wave sample (-128 to +127)
inline int32_t getWaveSample(WaveformType wave, uint32_t phase) {
    uint8_t phaseMSB = phase >> 24;
    switch (wave) {
        case WAVEFORM_SAWTOOTH:
            return (int16_t)phaseMSB - 128;
        case WAVEFORM_SQUARE:
            return (phaseMSB >= 128) ? 127 : -128;
        case WAVEFORM_TRIANGLE:
            return (phaseMSB < 128) ? ((phaseMSB << 1) - 128) : (383 - (phaseMSB << 1));
        case WAVEFORM_SINE:
            return sineLUT[phaseMSB];
        default:
            return 0;
    }
}

// ============================================================================
// PolyBLEP - Band-limited waveform synthesis
// Properly implements Polynomial Band-Limited Step function
// ============================================================================
inline float polyBLEP(float t, float dt) {
    if (t < dt) {
        t = t / dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

// Band-limited sawtooth
inline int32_t getBLSaw(uint32_t phase, uint32_t phaseInc) {
    float t = (phase >> 8) / (float)0x01000000;  // Normalize to 0-1
    float dt = phaseInc / (float)0x100000000;    // Phase increment as fraction
    
    // Standard sawtooth: 2*t - 1, scaled to -128..127
    float sample = (2.0f * t - 1.0f) * 127.0f;
    
    // Apply PolyBLEP correction at discontinuity (t=0)
    sample -= polyBLEP(t, dt) * 127.0f;
    
    return (int32_t)sample;
}

// Band-limited square
inline int32_t getBLSquare(uint32_t phase, uint32_t phaseInc) {
    float t = (phase >> 8) / (float)0x01000000;
    float dt = phaseInc / (float)0x100000000;
    
    // Standard square wave, scaled to -128..127
    float sample = (t < 0.5f) ? 127.0f : -128.0f;
    
    // Apply PolyBLEP at both edges (t=0 and t=0.5)
    sample -= polyBLEP(t, dt) * 255.0f;
    sample += polyBLEP(fmodf(t + 0.5f, 1.0f), dt) * 255.0f;
    
    return (int32_t)sample;
}

void sampleISR() {
    // ========================================================================
    // 1. Glide (Portamento)
    // ========================================================================
    if (currentStepSize != targetStepSize) {
        int32_t diff = targetStepSize - currentStepSize;
        int32_t glideFactor = 1 + (localParams.glideTime * 50);
        int32_t step = diff / glideFactor;
        if (abs(step) < 500) step = (diff > 0) ? 500 : -500;
        
        currentStepSize += step;
        
        if ((step > 0 && currentStepSize > targetStepSize) ||
            (step < 0 && currentStepSize < targetStepSize)) {
            currentStepSize = targetStepSize;
        }
    }

    // ========================================================================
    // 2. LFO
    // ========================================================================
    uint32_t lfoInc = 5000 + (localParams.lfoRate * 3000);
    lfoPhase += lfoInc;
    int32_t lfoVal = getWaveSample(WAVEFORM_TRIANGLE, lfoPhase); // -128 to 127

    // Noise Generation (White noise from LFSR)
    uint32_t bit = ((lfsrState >> 0) ^ (lfsrState >> 2) ^ 
                    (lfsrState >> 3) ^ (lfsrState >> 5)) & 1;
    lfsrState = (lfsrState >> 1) | (bit << 31);
    int32_t noiseVal = (int32_t)(lfsrState >> 24) - 128;

    // Sample and Hold (sample noise at LFO cycle start)
    if (lfoPhase < lastLfoPhase) {
        shValue = noiseVal;
    }
    lastLfoPhase = lfoPhase;

    // ========================================================================
    // 3. Modulation Envelope
    // ========================================================================
    int32_t modEnvCurrent = 0;
    switch(modEnvState) {
        case ENV_ATTACK: {
            int32_t step = (255 << 8) / (1 + localParams.modEnvAttack * 2);
            modEnvValue += step;
            if (modEnvValue >= (255 << 8)) {
                modEnvValue = 255 << 8;
                modEnvState = ENV_DECAY;
            }
            break;
        }
        case ENV_DECAY: {
            int32_t step = (255 << 8) / (1 + localParams.modEnvDecay * 2);
            modEnvValue -= step;
            if (modEnvValue <= 0) {
                modEnvValue = 0;
                modEnvState = ENV_IDLE;
            }
            break;
        }
        case ENV_RELEASE:
            modEnvValue -= (255 << 8) / (1 + localParams.modEnvDecay);
            if (modEnvValue <= 0) {
                modEnvValue = 0;
                modEnvState = ENV_IDLE;
            }
            break;
        default:
            modEnvValue = 0;
            break;
    }
    modEnvCurrent = ((modEnvValue >> 8) * localParams.modEnvAmount) / 64;

    // ========================================================================
    // 4. Pitch Calculation with Modulations
    // ========================================================================
    uint32_t osc1Step = currentStepSize;
    
    // LFO to pitch
    if (localParams.lfoDepth > 0 && localParams.lfoTarget == 0) {
        int32_t mod = (lfoVal * localParams.lfoDepth) >> 7;
        osc1Step += (osc1Step * mod) >> 10;
    }
    
    // Mod envelope to pitch
    if (localParams.modEnvTarget == 0) {
        osc1Step += (osc1Step * modEnvCurrent) >> 10;
    }
    
    // Sample & Hold to pitch
    if (localParams.shTarget == 0 && localParams.shDepth > 0) {
        int32_t mod = (shValue * localParams.shDepth) >> 7;
        osc1Step += (osc1Step * mod) >> 10;
    }

    // OSC2 tuning
    uint32_t osc2Step = osc1Step;
    
    // Detune (cents)
    if (localParams.osc2Detune != 0) {
        int32_t detuneFactor = 4096 + (localParams.osc2Detune * 4);
        osc2Step = (osc2Step * detuneFactor) >> 12;
    }
    
    // Octave shift
    if (localParams.osc2Octave > 0) {
        osc2Step <<= localParams.osc2Octave;
    } else if (localParams.osc2Octave < 0) {
        osc2Step >>= (-localParams.osc2Octave);
    }
    
    // Mod envelope to OSC2 pitch
    if (localParams.modEnvTarget == 2) {
        osc2Step += (osc2Step * modEnvCurrent) >> 10;
    }

    // ========================================================================
    // 5. Oscillators
    // ========================================================================
    uint32_t oldPhase1 = phase1;
    
    phase1 += osc1Step;
    phase2 += osc2Step;

    // Hard sync: reset OSC2 phase when OSC1 wraps
    if (localParams.oscSync && phase1 < oldPhase1) {
        phase2 = 0;
    }

    // OSC1 with morphing between waveforms
    uint8_t waveIndex1 = localParams.osc1WaveMorph >> 6;  // 0-3
    uint8_t waveIndex2 = waveIndex1 + 1;
    if (waveIndex2 > 3) waveIndex2 = 3;
    uint8_t morphFrac = (localParams.osc1WaveMorph & 0x3F) << 2; // 0-255

    int32_t osc1a = getWaveSample((WaveformType)waveIndex1, phase1);
    int32_t osc1b = getWaveSample((WaveformType)waveIndex2, phase1);
    int32_t osc1Out = ((osc1a * (255 - morphFrac)) + (osc1b * morphFrac)) >> 8;

    // OSC2
    int32_t osc2Out = getWaveSample(localParams.osc2Wave, phase2);

    // Sub oscillator (1 octave below OSC1)
    int32_t subOut = ((phase1 & 0x80000000) ? 127 : -128);

    // Ring modulation (OSC1 * OSC2)
    int32_t ringOut = (osc1Out * osc2Out) >> 7;

    // ========================================================================
    // 6. Mixer
    // ========================================================================
    int32_t mix = localParams.mixOsc2;
    int32_t vout = ((osc1Out * (100 - mix)) + (osc2Out * mix)) / 100;
    
    vout += (subOut * localParams.subOscMix) / 100;
    vout += (noiseVal * localParams.noiseMix) / 100;
    vout += (ringOut * localParams.ringModMix) / 100;

    // Soft clip the mix bus
    if (vout > 127) vout = 127 + ((vout - 127) >> 2);
    if (vout < -128) vout = -128 - ((-128 - vout) >> 2);

    // ========================================================================
    // 7. Wavefolder
    // ========================================================================
    if (localParams.wavefold > 0) {
        int32_t threshold = 127 - (localParams.wavefold >> 1);
        if (threshold < 32) threshold = 32;
        
        int32_t gain = 1 + (localParams.wavefold >> 4);
        int32_t folded = vout * gain;
        
        while (folded > threshold || folded < -threshold) {
            if (folded > threshold) {
                folded = (threshold << 1) - folded;
            } else if (folded < -threshold) {
                folded = -(threshold << 1) - folded;
            }
        }
        vout = folded;
    }

    // ========================================================================
    // 8. VCA Envelope
    // ========================================================================
    int32_t envCurrent = 0;
    switch(envState) {
        case ENV_ATTACK: {
            int32_t step = (255 << 8) / (1 + localParams.envAttack * 2);
            envValue += step;
            if (envValue >= (255 << 8)) {
                envValue = 255 << 8;
                envState = ENV_DECAY;
            }
            break;
        }
        case ENV_DECAY: {
            int32_t target = localParams.envSustain << 9;
            int32_t step = (255 << 8) / (1 + localParams.envDecay * 2);
            envValue -= step;
            if (envValue <= target) {
                envValue = target;
                envState = ENV_SUSTAIN;
            }
            break;
        }
        case ENV_SUSTAIN:
            envValue = localParams.envSustain << 9;
            break;
        case ENV_RELEASE: {
            int32_t step = (255 << 8) / (1 + localParams.envRelease * 2);
            envValue -= step;
            if (envValue <= 0) {
                envValue = 0;
                envState = ENV_IDLE;
            }
            break;
        }
        case ENV_IDLE:
            envValue = 0;
            break;
    }
    envCurrent = envValue >> 8; // 0-255

    // Apply VCA
    vout = (vout * envCurrent) >> 8;

    // ========================================================================
    // 9. Filter Section
    // ========================================================================
    int32_t cutoff = smoothCutoff * 2;  // 0-255 range
    
    // Filter modulation
    if (localParams.lfoTarget == 1 && localParams.lfoDepth > 0) {
        cutoff += (lfoVal * localParams.lfoDepth) >> 6;
    }
    cutoff += (envCurrent * localParams.filterEnvDepth) >> 6;
    
    if (localParams.modEnvTarget == 1) {
        cutoff += modEnvCurrent >> 1;
    }
    if (localParams.shTarget == 1 && localParams.shDepth > 0) {
        cutoff += (shValue * localParams.shDepth) >> 6;
    }
    
    // Clamp cutoff
    if (cutoff < 1) cutoff = 1;
    if (cutoff > 255) cutoff = 255;

    // Pre-filter drive
    if (localParams.filterDrive > 0) {
        int32_t gain = 1 + (localParams.filterDrive >> 4);
        vout *= gain;
        // Soft clip
        if (vout > 255) vout = 255 + ((vout - 255) >> 2);
        if (vout < -256) vout = -256 - ((-256 - vout) >> 2);
    }

    // Apply selected filter model
    if (localParams.filterModel == 1) {
        // === Moog Ladder Filter (4-pole LP) ===
        int32_t q = localParams.filterRes * 2;  // 0-254
        int32_t f = cutoff;
        
        // Feedback for resonance
        int32_t fb = (moog_s[3] * q) >> 8;
        int32_t input = vout - fb;
        
        // Clamp input
        if (input > 255) input = 255;
        if (input < -256) input = -256;
        
        // 4 cascaded one-pole filters
        moog_s[0] += ((input - moog_s[0]) * f) >> 8;
        moog_s[1] += ((moog_s[0] - moog_s[1]) * f) >> 8;
        moog_s[2] += ((moog_s[1] - moog_s[2]) * f) >> 8;
        moog_s[3] += ((moog_s[2] - moog_s[3]) * f) >> 8;
        
        switch(localParams.filterType) {
            case 0: vout = moog_s[3]; break;  // LP
            case 1: vout = vout - moog_s[3]; break;  // HP approx
            case 2: vout = moog_s[2] - moog_s[3]; break;  // BP approx
            case 3: vout = vout - moog_s[2]; break;  // Notch
        }
        
    } else if (localParams.filterModel == 2) {
        // === MS-20 Style Filter ===
        int32_t q = localParams.filterRes;
        int32_t f = cutoff;
        
        // Resonance feedback
        int32_t fb = (ms20_s[1] * q) >> 7;
        int32_t input = vout - fb;
        
        // Asymmetric clipping
        if (input > 180) input = 180;
        if (input < -200) input = -200;
        
        ms20_s[0] += ((input - ms20_s[0]) * f) >> 8;
        ms20_s[1] += ((ms20_s[0] - ms20_s[1]) * f) >> 8;
        
        switch(localParams.filterType) {
            case 0: vout = ms20_s[1]; break;  // LP
            case 1: vout = input - ms20_s[0]; break;  // HP
            case 2: vout = ms20_s[0] - ms20_s[1]; break;  // BP
            case 3: vout = ms20_s[1] + (input - ms20_s[0]); break;  // Notch
        }
        
    } else {
        // === Standard State Variable Filter ===
        int32_t q = 255 - (localParams.filterRes * 2);
        if (q < 8) q = 8;
        
        f_lp += ((cutoff * f_bp) >> 8);
        int32_t f_hp = vout - f_lp - ((q * f_bp) >> 8);
        f_bp += ((cutoff * f_hp) >> 8);
        
        switch(localParams.filterType) {
            case 0: vout = f_lp; break;  // LP
            case 1: vout = f_hp; break;  // HP
            case 2: vout = f_bp; break;  // BP
            case 3: vout = f_lp + f_hp; break;  // Notch
        }
    }

    // ========================================================================
    // 10. Delay Effect
    // ========================================================================
    if (localParams.delayTime > 0) {
        uint32_t delayLen = ((uint32_t)localParams.delayTime * DELAY_BUFFER_SIZE) / 128;
        if (delayLen < 100) delayLen = 100;
        
        uint32_t readPos = (delayWritePos + DELAY_BUFFER_SIZE - delayLen) % DELAY_BUFFER_SIZE;
        int32_t delayOut = delayBuffer[readPos];
        
        // Write with feedback
        int32_t feedback = (delayOut * localParams.delayFeedback) >> 7;
        delayBuffer[delayWritePos] = vout + feedback;
        delayWritePos = (delayWritePos + 1) % DELAY_BUFFER_SIZE;
        
        // Mix
        int32_t wet = (delayOut * localParams.delayMix) >> 7;
        vout = vout + wet;
    }

    // ========================================================================
    // 11. Chorus Effect
    // ========================================================================
    if (localParams.chorusDepth > 0 && localParams.chorusMix > 0) {
        chorusLfoPhase += 2000 + (localParams.chorusRate * 2000);
        int32_t chorusLfo = getWaveSample(WAVEFORM_SINE, chorusLfoPhase);
        
        uint32_t baseDelay = 256;
        int32_t modulation = (chorusLfo * localParams.chorusDepth) >> 3;
        int32_t delayAmt = baseDelay + modulation;
        
        if (delayAmt < 10) delayAmt = 10;
        if (delayAmt >= CHORUS_BUFFER_SIZE) delayAmt = CHORUS_BUFFER_SIZE - 1;
        
        uint32_t readPos = (chorusWritePos + CHORUS_BUFFER_SIZE - delayAmt) % CHORUS_BUFFER_SIZE;
        int32_t chorusOut = chorusBuffer[readPos];
        
        chorusBuffer[chorusWritePos] = vout;
        chorusWritePos = (chorusWritePos + 1) % CHORUS_BUFFER_SIZE;
        
        int32_t wet = (chorusOut * localParams.chorusMix) / 100;
        vout = vout + (wet >> 1);
    }

    // ========================================================================
    // 12. Bitcrusher / Decimator
    // ========================================================================
    if (localParams.decimatorRate > 0) {
        decimatorCounter++;
        uint8_t holdTime = (localParams.decimatorRate >> 4) + 1;
        if (decimatorCounter >= holdTime) {
            decimatorCounter = 0;
            lastDecimatorVal = vout;
        } else {
            vout = lastDecimatorVal;
        }
    }

    if (localParams.bitcrushDepth > 0) {
        uint8_t bits = 8 - localParams.bitcrushDepth;
        if (bits < 1) bits = 1;
        int32_t shift = 8 - bits;
        vout = (vout >> shift) << shift;
    }

    // ========================================================================
    // 13. Master Output
    // ========================================================================
    // Apply master volume
    vout = (vout * smoothVol) >> 3;

    // DC blocking (simple highpass)
    static int32_t dcPrevIn = 0;
    static int32_t dcPrevOut = 0;
    int32_t dcIn = vout;
    vout = dcIn - dcPrevIn + ((dcPrevOut * 1020) >> 10);  // ~0.996
    dcPrevIn = dcIn;
    dcPrevOut = vout;

    // Final limiting
    if (vout > 127) vout = 127;
    if (vout < -128) vout = -128;

    // Output to PWM DAC
    analogWrite(OUTR_PIN, vout + 128);
}
