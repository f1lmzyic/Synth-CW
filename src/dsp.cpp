#include "dsp.h"
#include <Arduino.h>
#include "constants.h"
#include "sine_lut.h"

const char* waveformNames[] = {"Saw", "Sqr", "Tri", "Sin", "-"};

// Local copy of params for lock-free ISR access
static SynthParams localParams;
static volatile uint32_t currentStepSize = 0;
static volatile uint32_t targetStepSize = 0;
static volatile uint32_t osc1StepSize = 0;
static volatile uint32_t osc2StepSize = 0;

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

// Note frequencies
const uint32_t baseStepSizes[] = {51076057, 54113183, 57330935, 60740010, 64351798,
                                  68178355, 72232452, 76527617, 81078186, 85899345,
                                  91007186, 96418755};

void dspInit() {
    memset(delayBuffer, 0, sizeof(delayBuffer));
    
    // Default params
    localParams.osc1WaveMorph = 0; // Sawtooth
    localParams.osc2Wave = WAVEFORM_SQUARE;
    localParams.osc2Detune = 0;
    localParams.osc2Octave = -1;
    localParams.mixOsc2 = 50;
    localParams.subOscMix = 0;
    localParams.noiseMix = 0;
    localParams.ringModMix = 0;
    
    localParams.filterCutoff = 127;
    localParams.filterRes = 0;
    localParams.filterEnvDepth = 0;
    localParams.filterType = 0;
    localParams.filterModel = 0;
    localParams.filterDrive = 0;
    localParams.wavefold = 0;
    localParams.oscSync = false;
    
    localParams.envAttack = 10;
    localParams.envDecay = 40;
    localParams.envSustain = 64;
    localParams.envRelease = 40;

    localParams.modEnvAttack = 10;
    localParams.modEnvDecay = 40;
    localParams.modEnvAmount = 0;
    localParams.modEnvTarget = 0;
    
    localParams.lfoRate = 10;
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
    
    localParams.masterVol = 4;

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
    
    // baseStepSizes are defined for C4 to B4 (MIDI 60-71)
    int noteIndex = note % 12;
    int octave = (note / 12) - 5; // MIDI 60 is C4, offset relative to base array
    
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
            
            // Get target note
            int key = sysState.pressedKey;
            
            // Normal Monophonic logic
            if (key >= 0) {
                targetStepSize = getStepSizeForMidiNote(key); // Compute directly
                if (envState == ENV_IDLE || envState == ENV_RELEASE) {
                    envState = ENV_ATTACK; // Trigger note
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
            
            xSemaphoreGive(sysState.mutex);
        }
    }
}

// Generate wave sample (-128 to +127)
inline int32_t getWaveSample(WaveformType wave, uint8_t phaseMSB) {
    switch (wave) {
        case WAVEFORM_SAWTOOTH:
            return phaseMSB - 128;
        case WAVEFORM_SQUARE:
            return (phaseMSB >= 128) ? 127 : -128;
        case WAVEFORM_TRIANGLE:
            return (phaseMSB < 128) ? ((phaseMSB << 1) - 128) : (383 - (phaseMSB << 1));
        case WAVEFORM_SINE: {
            return sineLUT[phaseMSB];
            
        }
        default:
            return 0;
    }
}

void sampleISR() {
    // 1. Glide calculation (Portamento)
    if (currentStepSize != targetStepSize) {
        int32_t diff = targetStepSize - currentStepSize;
        int32_t step = diff / (1 + (localParams.glideTime * 100)); // Rough approximation
        if (abs(step) < 1000) step = (diff > 0) ? 1000 : -1000;
        
        currentStepSize += step;
        
        if ((step > 0 && currentStepSize > targetStepSize) || 
            (step < 0 && currentStepSize < targetStepSize)) {
            currentStepSize = targetStepSize;
        }
    }

    // 2. LFO
    uint32_t lfoInc = 10000 + (localParams.lfoRate * 2000);
    lfoPhase += lfoInc;
    int32_t lfoVal = getWaveSample(WAVEFORM_TRIANGLE, lfoPhase >> 24); // -128 to 127
    
    // Noise Generation
    lfsrState = (lfsrState >> 1) ^ (-(lfsrState & 1) & 0xD0000001u);
    int32_t noiseVal = (int32_t)(lfsrState & 0xFF) - 128;
    
    // Sample and Hold
    if (lfoPhase < lastLfoPhase) {
        shValue = noiseVal;
    }
    lastLfoPhase = lfoPhase;
    
    // Mod Env
    int32_t modEnvStep = 0;
    switch(modEnvState) {
        case ENV_ATTACK:
            modEnvStep = 255 / (1 + localParams.modEnvAttack);
            modEnvValue += modEnvStep << 8;
            if (modEnvValue >= (255<<8)) { modEnvValue = 255<<8; modEnvState = ENV_DECAY; }
            break;
        case ENV_DECAY:
            modEnvStep = 255 / (1 + localParams.modEnvDecay);
            modEnvValue -= modEnvStep << 8;
            if (modEnvValue <= 0) { modEnvValue = 0; modEnvState = ENV_IDLE; }
            break;
        case ENV_IDLE:
        case ENV_SUSTAIN:
        case ENV_RELEASE:
            modEnvValue = 0;
            break;
    }
    int32_t modEnvCurrent = (modEnvValue >> 8) * localParams.modEnvAmount / 64; // -255 to 255
    
    // 3. Pitch Mod
    uint32_t modStep1 = currentStepSize;
    if (localParams.lfoDepth > 0 && localParams.lfoTarget == 0) {
        modStep1 += (currentStepSize / 1024) * ((lfoVal * localParams.lfoDepth) >> 7);
    }
    if (localParams.modEnvTarget == 0) {
        modStep1 += (currentStepSize / 1024) * modEnvCurrent;
    }
    if (localParams.shTarget == 0 && localParams.shDepth > 0) {
        modStep1 += (currentStepSize / 1024) * ((shValue * localParams.shDepth) >> 7);
    }
    
    // Osc2 Tuning
    int32_t osc2DetuneMultiplier = 4096 + (localParams.osc2Detune * 2); // 4096 is 1.0
    uint32_t modStep2 = (modStep1 * osc2DetuneMultiplier) >> 12;
    if (localParams.osc2Octave > 0) modStep2 <<= localParams.osc2Octave;
    else if (localParams.osc2Octave < 0) modStep2 >>= (-localParams.osc2Octave);
    
    if (localParams.modEnvTarget == 2) {
        modStep2 += (modStep2 / 1024) * modEnvCurrent;
    }

    // 4. Oscillators
    uint32_t oldPhase1 = phase1;
    phase1 += modStep1;
    phase2 += modStep2;
    
    if (localParams.oscSync && phase1 < oldPhase1) {
        phase2 = 0; // Hard sync
    }
    
    uint8_t waveBase1 = localParams.osc1WaveMorph >> 6; // 0-3
    uint8_t waveBase2 = waveBase1 + 1;
    if (waveBase2 > 3) waveBase2 = 3;
    uint8_t morphFract = (localParams.osc1WaveMorph & 0x3F) << 2; // 0-255

    int32_t s1a = getWaveSample((WaveformType)waveBase1, phase1 >> 24);
    int32_t s1b = getWaveSample((WaveformType)waveBase2, phase1 >> 24);
    int32_t sample1 = ((s1a * (255 - morphFract)) + (s1b * morphFract)) >> 8;
    
    int32_t sample2 = getWaveSample(localParams.osc2Wave, phase2 >> 24);
    
    int32_t subOscVal = (phase1 >= 0x80000000) ? 127 : -128; // Square wave 1 octave down
    int32_t ringModVal = (sample1 * sample2) >> 7; // -128 to 127
    
    // Mix
    int32_t mix = localParams.mixOsc2;
    int32_t vout = ((sample1 * (100 - mix)) + (sample2 * mix)) / 100;
    
    vout += (subOscVal * localParams.subOscMix) / 100;
    vout += (noiseVal * localParams.noiseMix) / 100;
    vout += (ringModVal * localParams.ringModMix) / 100;
    
    // Normalize mix
    vout = (vout > 127) ? 127 : ((vout < -128) ? -128 : vout);
    
    // Wavefolding
    if (localParams.wavefold > 0) {
        int32_t threshold = 127 - (localParams.wavefold >> 1); // 127 to 63
        if (threshold < 10) threshold = 10;
        
        int32_t folded = vout * (1 + (localParams.wavefold >> 5));
        
        while (folded > threshold || folded < -threshold) {
            if (folded > threshold) folded = 2 * threshold - folded;
            if (folded < -threshold) folded = -2 * threshold - folded;
        }
        vout = folded;
    }
    
    // 5. Envelope
    int32_t envStep = 0;
    switch(envState) {
        case ENV_ATTACK:
            envStep = 255 / (1 + localParams.envAttack);
            envValue += envStep << 8;
            if (envValue >= (255<<8)) { envValue = 255<<8; envState = ENV_DECAY; }
            break;
        case ENV_DECAY:
            envStep = 255 / (1 + localParams.envDecay);
            envValue -= envStep << 8;
            if (envValue <= (localParams.envSustain << 9)) { envValue = localParams.envSustain << 9; envState = ENV_SUSTAIN; }
            break;
        case ENV_SUSTAIN:
            envValue = localParams.envSustain << 9;
            break;
        case ENV_RELEASE:
            envStep = 255 / (1 + localParams.envRelease);
            envValue -= envStep << 8;
            if (envValue <= 0) { envValue = 0; envState = ENV_IDLE; }
            break;
        case ENV_IDLE:
            envValue = 0;
            break;
    }
    int32_t envCurrent = envValue >> 8; // 0 to 255
    
    // Apply VCA
    vout = (vout * envCurrent) >> 8;
    
    // 6. Filter (Multi-Model)
    int32_t cutoff = localParams.filterCutoff * 2; 
    
    // Filter Mod
    if (localParams.lfoTarget == 1 && localParams.lfoDepth > 0) {
        cutoff += (lfoVal * localParams.lfoDepth) >> 6;
    }
    cutoff += (envCurrent * localParams.filterEnvDepth) >> 6;
    
    if (localParams.modEnvTarget == 1) { // Mod Env to Filter
        cutoff += modEnvCurrent >> 1;
    }
    if (localParams.shTarget == 1 && localParams.shDepth > 0) { // S&H to Filter
        cutoff += (shValue * localParams.shDepth) >> 6;
    }
    
    if (cutoff < 0) cutoff = 0;
    if (cutoff > 255) cutoff = 255;

    // Apply Drive/Saturation
    int32_t driveAmount = localParams.filterDrive;
    if (driveAmount > 0) {
        // Simple overdrive: soft clip approach
        int32_t gain = 1 + (driveAmount >> 4);
        int32_t boosted = vout * gain;
        // Soft clip
        if (boosted > 127) boosted = 127 + ((boosted - 127) >> 1);
        if (boosted < -128) boosted = -128 - ((-128 - boosted) >> 1);
        // Hard clip just in case
        if (boosted > 255) boosted = 255;
        if (boosted < -256) boosted = -256;
        vout = boosted;
    }
    
    if (localParams.filterModel == 1) {
        // Moog Ladder approximation (4-pole lowpass)
        int32_t q = localParams.filterRes; // 0 to 127
        int32_t k = (q * 3) >> 1; // Resonance amount
        int32_t f = cutoff; // 0 to 255
        
        // Non-linear feedback (approximate)
        int32_t fb = (moog_s[3] * k) >> 7;
        
        int32_t input = vout - fb;
        // Soft clipping in the filter input for Moog character
        if (input > 255) input = 255;
        if (input < -256) input = -256;
        
        moog_s[0] += (f * (input - moog_s[0])) >> 8;
        moog_s[1] += (f * (moog_s[0] - moog_s[1])) >> 8;
        moog_s[2] += (f * (moog_s[1] - moog_s[2])) >> 8;
        moog_s[3] += (f * (moog_s[2] - moog_s[3])) >> 8;
        
        switch(localParams.filterType) {
            case 0: vout = moog_s[3]; break; // 4-pole LP
            case 1: vout = vout - moog_s[3]; break; // HP approx
            case 2: vout = moog_s[2] - moog_s[3]; break; // BP approx
            case 3: vout = vout - moog_s[2]; break; // Notch approx
        }
    } else if (localParams.filterModel == 2) {
        // MS-20 Sallen-Key approximation
        int32_t q = localParams.filterRes;
        int32_t f = cutoff;
        
        // Resonance feedback
        int32_t fb = (ms20_s[1] * q) >> 6;
        
        int32_t input = vout - fb;
        // Asymmetric clipping for MS-20 grittiness
        if (input > 200) input = 200 + ((input - 200) >> 1);
        if (input < -256) input = -256;
        
        ms20_s[0] += (f * (input - ms20_s[0])) >> 8;
        ms20_s[1] += (f * (ms20_s[0] - ms20_s[1])) >> 8;
        
        switch(localParams.filterType) {
            case 0: vout = ms20_s[1]; break; // LP
            case 1: vout = input - ms20_s[0]; break; // HP
            case 2: vout = ms20_s[0] - ms20_s[1]; break; // BP
            case 3: vout = ms20_s[1] + (input - ms20_s[0]); break; // Notch
        }
    } else {
        // Standard SVF
        int32_t q = 255 - (localParams.filterRes * 2);
        if (q < 10) q = 10;
        
        f_lp += (cutoff * f_bp) >> 8;
        int32_t f_hp = vout - f_lp - ((q * f_bp) >> 8);
        f_bp += (cutoff * f_hp) >> 8;
        
        switch(localParams.filterType) {
            case 0: vout = f_lp; break;
            case 1: vout = f_hp; break;
            case 2: vout = f_bp; break;
            case 3: vout = f_lp + f_hp; break;
        }
    }
    
    // 7. Delay
    if (localParams.delayTime > 0) {
        uint32_t delayLen = (localParams.delayTime * DELAY_BUFFER_SIZE) / 128;
        if (delayLen == 0) delayLen = 1;
        uint32_t readPos = (delayWritePos + DELAY_BUFFER_SIZE - delayLen) % DELAY_BUFFER_SIZE;
        int32_t dOut = delayBuffer[readPos];
        
        delayBuffer[delayWritePos] = vout + ((dOut * localParams.delayFeedback) / 128);
        delayWritePos = (delayWritePos + 1) % DELAY_BUFFER_SIZE;
        
        vout = vout + ((dOut * localParams.delayMix) / 128);
    }
    
    // 8. Chorus
    if (localParams.chorusDepth > 0) {
        chorusLfoPhase += (5000 + (localParams.chorusRate * 1000));
        int32_t cLfo = getWaveSample(WAVEFORM_SINE, chorusLfoPhase >> 24); // -128 to 127
        
        uint32_t baseDelay = 512;
        int32_t modulation = (cLfo * localParams.chorusDepth) >> 2; // up to +/- 256
        uint32_t delayAmount = baseDelay + modulation;
        
        uint32_t readPos = (chorusWritePos + CHORUS_BUFFER_SIZE - delayAmount) % CHORUS_BUFFER_SIZE;
        int32_t cOut = chorusBuffer[readPos];
        
        chorusBuffer[chorusWritePos] = vout;
        chorusWritePos = (chorusWritePos + 1) % CHORUS_BUFFER_SIZE;
        
        vout = ((vout * (100 - localParams.chorusMix)) + (cOut * localParams.chorusMix)) / 100;
    }
    
    // 9. Decimator (Sample Rate Reduction)
    if (localParams.decimatorRate > 0) {
        decimatorCounter++;
        if (decimatorCounter >= (localParams.decimatorRate >> 4) + 1) {
            decimatorCounter = 0;
            lastDecimatorVal = vout;
        } else {
            vout = lastDecimatorVal;
        }
    }
    
    // 10. Bitcrusher
    if (localParams.bitcrushDepth > 0) {
        uint8_t shift = localParams.bitcrushDepth; // 1-7
        if (shift > 7) shift = 7;
        vout = (vout >> shift) << shift;
    }
    
    // 11. Master Vol
    // Linear scaling (4 = 50% volume). Shifting by 3 handles scale of 8 safely
    vout = (vout * localParams.masterVol) >> 3;
    
    // Clamp and output
    if (vout > 127) vout = 127;
    if (vout < -128) vout = -128;
    
    analogWrite(OUTR_PIN, vout + 128);
}
