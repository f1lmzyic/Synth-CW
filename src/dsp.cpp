#include "dsp.h"
#include <Arduino.h>
#include "constants.h"

// Module state (static for ISR safety - no dynamic allocation)
static SynthParams localParams;
static FilterState filterState;
static EffectsState effectsState;
static LfoState lfoState;
static int32_t smoothCutoff = 255, smoothVol = 4;

void dspInit() {
    voiceEngineInit(); envelopeInit(); filterInit(filterState);
    effectsInit(&effectsState); lfoInit(&lfoState);
    localParams = {};
    localParams.osc2Wave = WAVEFORM_SAWTOOTH; localParams.osc2Detune = 5;
    localParams.mixOsc2 = 30; localParams.subOscMix = 20;
    localParams.filterCutoff = 100; localParams.filterRes = 30;
    localParams.filterEnvDepth = 50; localParams.envAttack = 15;
    localParams.envDecay = 60; localParams.envSustain = 40;
    localParams.envRelease = 50; localParams.modEnvAttack = 10;
    localParams.modEnvDecay = 40; localParams.lfoRate = 20;
    localParams.masterVol = 6;
    smoothCutoff = localParams.filterCutoff; smoothVol = localParams.masterVol;

    MutexGuard lock(sysState.mutex);
    if (lock) {
        sysState.params = localParams;
    }
}

void dspUpdateParams() {
    MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
    if (lock) {
        localParams = sysState.params;
        voiceEngineUpdateParams();
        smoothCutoff = localParams.filterCutoff;
        smoothVol = localParams.masterVol;
    }
}

void sampleISR() {
    // 1-4. LFO, Noise, S&H, Mod envelope
    int32_t lfoVal = processLFO(&lfoState, localParams.lfoRate);
    int32_t noiseVal = generateNoise(&lfoState);
    processSampleHold(&lfoState, noiseVal);
    int32_t modEnvCurrent = processModEnvelope(localParams);

    // 5-6. Polyphonic voice processing
    int32_t combinedVoiceOut = 0, totalEnvCurrent = 0;
    uint8_t activeVoiceCount = 0;
    for (int v = 0; v < POLYPHONY; v++) {
        volatile VoiceState& voice = voices[v];
        if (!voice.active) continue;
        activeVoiceCount++;
        // Voice glide
        if (voice.step != voice.targetStep) {
            int32_t diff = voice.targetStep - voice.step;
            int32_t step = diff / (1 + localParams.glideTime * 50);
            if (abs(step) < GLIDE_STEP_MIN) step = (diff > 0) ? GLIDE_STEP_MIN : -GLIDE_STEP_MIN;
            voice.step += step;
            if ((step > 0 && voice.step > voice.targetStep) ||
                (step < 0 && voice.step < voice.targetStep))
                voice.step = voice.targetStep;
        }
        // Voice pitch with modulations
        uint32_t osc1Step = voice.step;
        if (localParams.lfoDepth > 0 && localParams.lfoTarget == 0)
            osc1Step += (osc1Step * ((lfoVal * localParams.lfoDepth) >> 7)) >> 10;
        if (localParams.modEnvTarget == 0)
            osc1Step += (osc1Step * modEnvCurrent) >> 10;
        if (localParams.shTarget == 0 && localParams.shDepth > 0)
            osc1Step += (osc1Step * ((lfoState.shValue * localParams.shDepth) >> 7)) >> 10;
        // OSC2 tuning
        uint32_t osc2Step = osc1Step;
        if (localParams.osc2Detune != 0)
            osc2Step = (osc2Step * (4096 + localParams.osc2Detune * 4)) >> 12;
        if (localParams.osc2Octave > 0) osc2Step <<= localParams.osc2Octave;
        else if (localParams.osc2Octave < 0) osc2Step >>= -localParams.osc2Octave;
        if (localParams.modEnvTarget == 2) osc2Step += (osc2Step * modEnvCurrent) >> 10;
        // Advance phase
        voice.phase += osc1Step;
        // Mix oscillators, wavefolder, envelope, VCA
        int32_t voiceMix = mixOscillators(osc1Step, osc2Step, voice.phase, localParams, noiseVal);
        if (localParams.wavefold > 0) voiceMix = applyWavefolder(voiceMix, localParams.wavefold);
        uint8_t voiceEnv = processEnvelope(v, localParams);
        voiceMix = (voiceMix * voiceEnv) >> 8;
        combinedVoiceOut += voiceMix; totalEnvCurrent += voiceEnv;
    }
    // Release envelopes for inactive voices
    for (int v = 0; v < POLYPHONY; v++) {
        volatile VoiceState& voice = voices[v];
        if (!voice.active && voice.envValue > 0) {
            int32_t step = voice.envValue / (8 + ((localParams.envRelease * localParams.envRelease) >> 5));
            if (step < 1) step = 1;
            voice.envValue -= step;
            if (voice.envValue <= 0) { voice.envValue = 0; voice.envState = VOICE_ENV_IDLE; }
        }
    }
    // Scale output
    if (activeVoiceCount > 0) totalEnvCurrent = (totalEnvCurrent + activeVoiceCount / 2) / activeVoiceCount;
    int32_t vout = activeVoiceCount ? (combinedVoiceOut * POLYPHONY) / (POLYPHONY + activeVoiceCount) : 0;

    // 7-9. Filter section
    int32_t cutoff = smoothCutoff * 2;
    if (localParams.lfoTarget == 1 && localParams.lfoDepth > 0) cutoff += (lfoVal * localParams.lfoDepth) >> 6;
    cutoff += (totalEnvCurrent * localParams.filterEnvDepth) >> 6;
    if (localParams.modEnvTarget == 1) cutoff += modEnvCurrent >> 1;
    if (localParams.shTarget == 1 && localParams.shDepth > 0) cutoff += (lfoState.shValue * localParams.shDepth) >> 6;
    cutoff = cutoff < 1 ? 1 : (cutoff > 255 ? 255 : cutoff);
    if (localParams.filterDrive > 0) vout = applyFilterDrive(vout, localParams.filterDrive);
    switch (localParams.filterModel) {
        case 1: vout = processMoogFilter(filterState, vout, cutoff, localParams.filterRes, localParams.filterType); break;
        case 2: vout = processMS20Filter(filterState, vout, cutoff, localParams.filterRes, localParams.filterType); break;
        default: vout = processSVF(filterState, vout, cutoff, localParams.filterRes, localParams.filterType);
    }
    // 10-13. Effects chain
    vout = processDelay(&effectsState, vout, localParams.delayTime, localParams.delayFeedback, localParams.delayMix);
    vout = processChorus(&effectsState, vout, localParams.chorusRate, localParams.chorusDepth, localParams.chorusMix);
    vout = processBitcrusher(vout, localParams.bitcrushDepth);
    vout = processDecimator(&effectsState, vout, localParams.decimatorRate);
    // 14-17. Master output
    vout = (vout * smoothVol) >> 3;
    static int32_t dcPrevIn = 0, dcPrevOut = 0;
    int32_t dcIn = vout;
    vout = dcIn - dcPrevIn + ((dcPrevOut * 1020) >> 10);
    dcPrevIn = dcIn; dcPrevOut = vout;
    if (vout > 127) vout = 127; if (vout < -128) vout = -128;
    analogWrite(OUTR_PIN, vout + 128);
}
