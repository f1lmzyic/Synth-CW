#include "dsp.h"
#include "constants.h"
#include <Arduino.h>

// Module state (static for ISR safety - no dynamic allocation)
static SynthParams localParams;
static FilterState filterState;
static EffectsState effectsState;
static LfoState lfoState;
static int32_t smoothCutoff = 255, smoothVol = 4;

void dspInit() {
  voiceEngineInit();
  envelopeInit();
  filterInit(filterState);
  effectsInit(&effectsState);
  lfoInit(&lfoState);
  localParams = {};
  localParams.osc2Wave = WAVEFORM_SAWTOOTH;
  localParams.osc2Detune = 5;
  localParams.mixOsc2 = 30;
  localParams.subOscMix = 20;
  localParams.filterCutoff = 100;
  localParams.filterRes = 30;
  localParams.filterEnvDepth = 50;
  localParams.envAttack = 15;
  localParams.envDecay = 60;
  localParams.envSustain = 40;
  localParams.envRelease = 50;
  localParams.modEnvAttack = 10;
  localParams.modEnvDecay = 40;
  localParams.lfoRate = 20;
  localParams.masterVol = 6;
  // Subtle defaults (effects disabled by default, but LFO/mod env have some depth)
  localParams.lfoDepth = 15;
  localParams.lfoTarget = 1;  // Default: LFO modulates filter cutoff
  localParams.modEnvAmount = 20;
  // Keep effects at 0 (disabled) - user can enable via UI knobs
  smoothCutoff = localParams.filterCutoff;
  smoothVol = localParams.masterVol;

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
  // Pre-compute modulation values only if needed
  int32_t lfoVal = 0, noiseVal = 0, modEnvCurrent = 0;
  const bool needLfo = localParams.lfoDepth > 0;
  const bool needNoise = localParams.noiseMix > 0;
  const bool needModEnv = localParams.modEnvAmount > 0;
  const bool needSH = localParams.shDepth > 0;

  if (needLfo || needSH) {
    lfoVal = processLFO(&lfoState, localParams.lfoRate);
  }
  if (needNoise || needSH) {
    noiseVal = generateNoise(&lfoState);
    if (needSH) processSampleHold(&lfoState, noiseVal);
  }
  if (needModEnv) {
    modEnvCurrent = processModEnvelope(localParams);
  }

  // Single merged voice loop for active + release processing
  int32_t combinedVoiceOut = 0, totalEnvCurrent = 0;
  uint8_t activeVoiceCount = 0;
  const uint8_t glideShift = 4 + (localParams.glideTime >> 3);
  const uint8_t releaseShift = 3 + (localParams.envRelease >> 4);
  const bool hasOsc2 = localParams.mixOsc2 > 0 || localParams.osc2Wave != WAVEFORM_OFF;

  for (int v = 0; v < POLYPHONY; v++) {
    volatile VoiceState &voice = voices[v];

    if (voice.active) {
      activeVoiceCount++;
      // Voice glide
      if (voice.step != voice.targetStep) {
        int32_t diff = voice.targetStep - voice.step;
        int32_t step = diff >> glideShift;
        if (step == 0) step = (diff > 0) ? 1 : -1;
        voice.step += step;
        if ((diff > 0 && voice.step > voice.targetStep) ||
            (diff < 0 && voice.step < voice.targetStep))
          voice.step = voice.targetStep;
      }

      // Voice pitch with modulations (only if enabled)
      uint32_t osc1Step = voice.step;
      if (needLfo && localParams.lfoTarget == 0)
        osc1Step += (osc1Step * ((lfoVal * localParams.lfoDepth) >> 7)) >> 10;
      if (needModEnv && localParams.modEnvTarget == 0)
        osc1Step += (osc1Step * modEnvCurrent) >> 10;
      if (needSH && localParams.shTarget == 0)
        osc1Step += (osc1Step * ((lfoState.shValue * localParams.shDepth) >> 7)) >> 10;

      // OSC2 tuning (only if OSC2 is used)
      uint32_t osc2Step = osc1Step;
      if (hasOsc2) {
        if (localParams.osc2Detune != 0)
          osc2Step = (osc2Step * (4096 + localParams.osc2Detune * 4)) >> 12;
        if (localParams.osc2Octave > 0)
          osc2Step <<= localParams.osc2Octave;
        else if (localParams.osc2Octave < 0)
          osc2Step >>= -localParams.osc2Octave;
        if (needModEnv && localParams.modEnvTarget == 2)
          osc2Step += (osc2Step * modEnvCurrent) >> 10;
      }

      // Advance phase and mix
      voice.phase += osc1Step;
      int32_t voiceMix = mixOscillators(osc1Step, osc2Step, voice.phase, localParams, noiseVal);
      if (localParams.wavefold > 0)
        voiceMix = applyWavefolder(voiceMix, localParams.wavefold);
      uint8_t voiceEnv = processEnvelope(v, localParams);
      voiceMix = (voiceMix * voiceEnv) >> 8;
      combinedVoiceOut += voiceMix;
      totalEnvCurrent += voiceEnv;
    } else if (voice.envValue > 0) {
      // Release envelope for inactive voice
      int32_t step = voice.envValue >> releaseShift;
      if (step < 1) step = 1;
      voice.envValue -= step;
      if (voice.envValue <= 0) {
        voice.envValue = 0;
        voice.envState = VOICE_ENV_IDLE;
      }
    }
  }

  // Early exit if no voices active
  int32_t vout = 0;
  if (activeVoiceCount == 0) {
    // Still need to process effects tails
    if (localParams.delayMix > 0)
      vout = processDelay(&effectsState, 0, localParams.delayTime,
                          localParams.delayFeedback, localParams.delayMix);
    goto output;
  }

  // Scale output (optimized switch)
  switch (activeVoiceCount) {
    case 1: vout = combinedVoiceOut >> 1; break;
    case 2: totalEnvCurrent >>= 1; vout = (combinedVoiceOut * 85) >> 8; break;
    case 3: totalEnvCurrent = (totalEnvCurrent * 85) >> 8; vout = (combinedVoiceOut * 73) >> 8; break;
    default: totalEnvCurrent >>= 2; vout = combinedVoiceOut >> 2; break;
  }

  // Filter section
  {
    int32_t cutoff = smoothCutoff * 2;
    if (needLfo && localParams.lfoTarget == 1)
      cutoff += (lfoVal * localParams.lfoDepth) >> 6;
    cutoff += (totalEnvCurrent * localParams.filterEnvDepth) >> 6;
    if (needModEnv && localParams.modEnvTarget == 1)
      cutoff += modEnvCurrent >> 1;
    if (needSH && localParams.shTarget == 1)
      cutoff += (lfoState.shValue * localParams.shDepth) >> 6;
    if (cutoff < 1) cutoff = 1;
    else if (cutoff > 255) cutoff = 255;

    if (localParams.filterDrive > 0)
      vout = applyFilterDrive(vout, localParams.filterDrive);

    switch (localParams.filterModel) {
      case 1:
        vout = processMoogFilter(filterState, vout, cutoff, localParams.filterRes, localParams.filterType);
        break;
      case 2:
        vout = processMS20Filter(filterState, vout, cutoff, localParams.filterRes, localParams.filterType);
        break;
      default:
        vout = processSVF(filterState, vout, cutoff, localParams.filterRes, localParams.filterType);
    }
  }

  // Effects chain (skip if disabled)
  if (localParams.delayTime > 0 && localParams.delayMix > 0)
    vout = processDelay(&effectsState, vout, localParams.delayTime,
                        localParams.delayFeedback, localParams.delayMix);
  if (localParams.chorusDepth > 0 && localParams.chorusMix > 0)
    vout = processChorus(&effectsState, vout, localParams.chorusRate,
                         localParams.chorusDepth, localParams.chorusMix);
  if (localParams.bitcrushDepth > 0)
    vout = processBitcrusher(vout, localParams.bitcrushDepth);
  if (localParams.decimatorRate > 0)
    vout = processDecimator(&effectsState, vout, localParams.decimatorRate);

output:
  // Master output with DC blocking
  vout = (vout * smoothVol) >> 3;
  static int32_t dcPrevIn = 0, dcPrevOut = 0;
  int32_t dcIn = vout;
  vout = dcIn - dcPrevIn + ((dcPrevOut * 1020) >> 10);
  dcPrevIn = dcIn;
  dcPrevOut = vout;
  if (vout > 127) vout = 127;
  else if (vout < -128) vout = -128;
  analogWrite(OUTR_PIN, vout + 128);
}
