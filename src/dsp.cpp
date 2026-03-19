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
  localParams.lfoRate = 20;
  localParams.masterVol = 6;
  // Subtle defaults
  localParams.lfoDepth = 15;
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
  // 1. LFO and Noise
  int32_t lfoVal = processLFO(&lfoState, localParams.lfoRate);
  int32_t noiseVal = generateNoise(&lfoState);

  // Pre-compute loop-invariant values (saves repeated work inside 4-voice loop)
  const uint8_t glideShift = 4 + (localParams.glideTime >> 3);
  const uint8_t releaseShift = 3 + (localParams.envRelease >> 4);
  const int32_t lfoScaledDepth =
      (localParams.lfoDepth > 0) ? ((lfoVal * localParams.lfoDepth) >> 7) : 0;
  const uint32_t osc2DetuneFactor =
      4096 + (int32_t)localParams.osc2Detune * 4;

  // 2. Polyphonic voice processing + release tails — single pass
  int32_t combinedVoiceOut = 0, totalEnvCurrent = 0;
  uint8_t activeVoiceCount = 0;

  for (int v = 0; v < POLYPHONY; v++) {
    volatile VoiceState &voice = voices[v];

    if (!voice.active) {
      // Release tail for inactive voices
      if (voice.envValue > 0) {
        int32_t step = voice.envValue >> releaseShift;
        if (step < 1) step = 1;
        voice.envValue -= step;
        if (voice.envValue <= 0) {
          voice.envValue = 0;
          voice.envState = VOICE_ENV_IDLE;
        }
      }
      continue;
    }
    activeVoiceCount++;

    // Load volatile fields once — enables register allocation for glide/pitch
    uint32_t v_step = voice.step;
    const uint32_t v_targetStep = voice.targetStep;

    // Voice glide
    if (v_step != v_targetStep) {
      int32_t diff = (int32_t)v_targetStep - (int32_t)v_step;
      int32_t glide = diff >> glideShift;
      if (glide == 0) glide = (diff > 0) ? 1 : -1;
      v_step += (uint32_t)glide;
      if ((diff > 0 && (int32_t)v_step > (int32_t)v_targetStep) ||
          (diff < 0 && (int32_t)v_step < (int32_t)v_targetStep))
        v_step = v_targetStep;
      voice.step = v_step;
    }

    // Voice pitch with LFO modulation (LFO modulates filter cutoff)
    uint32_t osc1Step = v_step;

    // OSC2 tuning
    uint32_t osc2Step = osc1Step;
    if (localParams.osc2Detune != 0)
      osc2Step = (osc2Step * osc2DetuneFactor) >> 12;
    if (localParams.osc2Octave > 0)
      osc2Step <<= localParams.osc2Octave;
    else if (localParams.osc2Octave < 0)
      osc2Step >>= -(int)localParams.osc2Octave;

    // Advance phase (local to avoid extra volatile load in mixOscillators arg)
    uint32_t v_phase = voice.phase + osc1Step;
    voice.phase = v_phase;

    // Mix oscillators, wavefolder, envelope, VCA
    int32_t voiceMix =
        mixOscillators(osc1Step, osc2Step, v_phase, localParams, noiseVal);
    if (localParams.wavefold > 0)
      voiceMix = applyWavefolder(voiceMix, localParams.wavefold);
    uint8_t voiceEnv = processEnvelope(v, localParams);
    voiceMix = (voiceMix * voiceEnv) >> 8;
    combinedVoiceOut += voiceMix;
    totalEnvCurrent += voiceEnv;
  }

  // Scale output using shift approximations to avoid division
  int32_t vout = 0;
  if (activeVoiceCount > 0) {
    switch (activeVoiceCount) {
      case 1: vout = combinedVoiceOut >> 1; break;
      case 2: totalEnvCurrent >>= 1; vout = (combinedVoiceOut * 85) >> 8; break;
      case 3: totalEnvCurrent = (totalEnvCurrent * 85) >> 8; vout = (combinedVoiceOut * 73) >> 8; break;
      case 4: totalEnvCurrent >>= 2; vout = combinedVoiceOut >> 2; break;
      default: totalEnvCurrent >>= 2; vout = combinedVoiceOut >> 2; break;
    }
  }

  // 3. Filter section (SVF only)
  int32_t cutoff = smoothCutoff * 2;
  // LFO modulates filter cutoff
  if (localParams.lfoDepth > 0)
    cutoff += (lfoVal * localParams.lfoDepth) >> 6;
  // Envelope modulates filter cutoff
  cutoff += (totalEnvCurrent * localParams.filterEnvDepth) >> 6;
  cutoff = cutoff < 1 ? 1 : (cutoff > 255 ? 255 : cutoff);
  vout = processSVF(filterState, vout, cutoff, localParams.filterRes,
                     localParams.filterType);

  // 4. Delay effect (only process when enabled)
  if (localParams.delayTime > 0)
    vout = processDelay(&effectsState, vout, localParams.delayTime,
                        localParams.delayFeedback, localParams.delayMix);

  // 5. Master output
  vout = (vout * smoothVol) >> 3;
  static int32_t dcPrevIn = 0, dcPrevOut = 0;
  int32_t dcIn = vout;
  vout = dcIn - dcPrevIn + ((dcPrevOut * 1020) >> 10);
  dcPrevIn = dcIn;
  dcPrevOut = vout;
  if (vout > 127)
    vout = 127;
  if (vout < -128)
    vout = -128;
  analogWrite(OUTR_PIN, vout + 128);
}
