#include "performance_features.h"
#include <Arduino.h>
#include <string.h>

static const uint16_t MAX_PHRASE_EVENTS = 128;

struct PhraseEvent
{
    uint32_t timeMs;                  // time from recording start
    uint16_t keys[MAX_PRESSED_KEYS];  // note set at this time
    uint8_t keyCount;
};

static PhraseEvent phraseEvents[MAX_PHRASE_EVENTS];
static uint16_t phraseEventCount = 0;

static uint32_t recordStartMs = 0;
static uint32_t playbackStartMs = 0;
static uint32_t phraseLengthMs = 0;

static bool prevRecEnabled = false;
static bool prevPlayEnabled = false;

static uint16_t lastRecordedKeys[MAX_PRESSED_KEYS];
static uint8_t lastRecordedCount = 0;

static uint16_t playbackKeys[MAX_PRESSED_KEYS];
static uint8_t playbackKeyCount = 0;

static void clearKeyArray(uint16_t *keys)
{
    for (uint8_t i = 0; i < MAX_PRESSED_KEYS; i++)
        keys[i] = 0xFFFF;
}

static void clearPlaybackState()
{
    playbackKeyCount = 0;
    clearKeyArray(playbackKeys);
}

static void applyPlaybackStateToSynth()
{
    sysState.synthKeyCount = playbackKeyCount;
    for (uint8_t i = 0; i < MAX_PRESSED_KEYS; i++)
    {
        if (i < playbackKeyCount)
            sysState.synthKeys[i] = playbackKeys[i];
        else
            sysState.synthKeys[i] = 0xFFFF;
    }
}

static void copyCurrentSynth(uint16_t *dstKeys, uint8_t &dstCount)
{
    dstCount = sysState.synthKeyCount;
    for (uint8_t i = 0; i < MAX_PRESSED_KEYS; i++)
    {
        if (i < sysState.synthKeyCount)
            dstKeys[i] = sysState.synthKeys[i];
        else
            dstKeys[i] = 0xFFFF;
    }
}

static bool sameKeyState(const uint16_t *aKeys, uint8_t aCount,
                         const uint16_t *bKeys, uint8_t bCount)
{
    if (aCount != bCount)
        return false;

    for (uint8_t i = 0; i < aCount; i++)
    {
        if (aKeys[i] != bKeys[i])
            return false;
    }
    return true;
}

static void storeEvent(uint32_t relMs, const uint16_t *keys, uint8_t keyCount)
{
    if (phraseEventCount >= MAX_PHRASE_EVENTS)
        return;

    phraseEvents[phraseEventCount].timeMs = relMs;
    phraseEvents[phraseEventCount].keyCount = keyCount;

    for (uint8_t i = 0; i < MAX_PRESSED_KEYS; i++)
    {
        if (i < keyCount)
            phraseEvents[phraseEventCount].keys[i] = keys[i];
        else
            phraseEvents[phraseEventCount].keys[i] = 0xFFFF;
    }

    phraseEventCount++;
}

static void startRecording()
{
    phraseEventCount = 0;
    phraseLengthMs = 0;
    recordStartMs = millis();

    uint16_t currentKeys[MAX_PRESSED_KEYS];
    uint8_t currentCount = 0;
    copyCurrentSynth(currentKeys, currentCount);

    for (uint8_t i = 0; i < MAX_PRESSED_KEYS; i++)
        lastRecordedKeys[i] = currentKeys[i];
    lastRecordedCount = currentCount;

    storeEvent(0, currentKeys, currentCount);
}

static void stopRecording()
{
    uint32_t now = millis();
    phraseLengthMs = now - recordStartMs;

    if (phraseLengthMs < 1)
        phraseLengthMs = 1;
}

static void recordChangesIfNeeded()
{
    uint16_t currentKeys[MAX_PRESSED_KEYS];
    uint8_t currentCount = 0;
    copyCurrentSynth(currentKeys, currentCount);

    if (!sameKeyState(currentKeys, currentCount, lastRecordedKeys, lastRecordedCount))
    {
        uint32_t relMs = millis() - recordStartMs;
        storeEvent(relMs, currentKeys, currentCount);

        for (uint8_t i = 0; i < MAX_PRESSED_KEYS; i++)
            lastRecordedKeys[i] = currentKeys[i];
        lastRecordedCount = currentCount;
    }
}

static void startPlayback()
{
    playbackStartMs = millis();

    if (phraseEventCount > 0)
    {
        playbackKeyCount = phraseEvents[0].keyCount;
        for (uint8_t i = 0; i < MAX_PRESSED_KEYS; i++)
            playbackKeys[i] = phraseEvents[0].keys[i];
    }
    else
    {
        clearPlaybackState();
    }
}

static void updatePlayback()
{
    if (phraseEventCount == 0 || phraseLengthMs == 0)
    {
        clearPlaybackState();
        applyPlaybackStateToSynth();
        return;
    }

    uint32_t elapsed = (millis() - playbackStartMs) % phraseLengthMs;

    uint16_t eventIndex = 0;
    for (uint16_t i = 0; i < phraseEventCount; i++)
    {
        if (phraseEvents[i].timeMs <= elapsed)
            eventIndex = i;
        else
            break;
    }

    playbackKeyCount = phraseEvents[eventIndex].keyCount;
    for (uint8_t i = 0; i < MAX_PRESSED_KEYS; i++)
        playbackKeys[i] = phraseEvents[eventIndex].keys[i];

    applyPlaybackStateToSynth();
}

void automationInit()
{
    memset(phraseEvents, 0, sizeof(phraseEvents));
    phraseEventCount = 0;
    phraseLengthMs = 0;
    prevRecEnabled = false;
    prevPlayEnabled = false;
    clearPlaybackState();
    clearKeyArray(lastRecordedKeys);
    lastRecordedCount = 0;
    sysState.autoStepIndex = 0;
    sysState.autoLastStepMs = millis();
}

void automationClear()
{
    memset(phraseEvents, 0, sizeof(phraseEvents));
    phraseEventCount = 0;
    phraseLengthMs = 0;
    prevRecEnabled = false;
    prevPlayEnabled = false;
    clearPlaybackState();
    clearKeyArray(lastRecordedKeys);
    lastRecordedCount = 0;
}

const char* automationMaskName(uint8_t mask)
{
    (void)mask;
    return "NOTE";
}

void automationStepTick()
{
    bool rec = sysState.params.autoRecEnabled;
    bool play = sysState.params.autoPlayEnabled;

    // Rising edge: REC ON
    if (rec && !prevRecEnabled)
    {
        sysState.params.autoPlayEnabled = false;
        startRecording();
    }

    // Falling edge: REC OFF
    if (!rec && prevRecEnabled)
    {
        stopRecording();
    }

    // Rising edge: PLAY ON
    if (play && !prevPlayEnabled)
    {
        sysState.params.autoRecEnabled = false;
        startPlayback();
    }

    // If recording, capture note changes with timestamps
    if (sysState.params.autoRecEnabled)
    {
        recordChangesIfNeeded();
    }

    // If playing, override synth output from recorded phrase
    if (sysState.params.autoPlayEnabled)
    {
        updatePlayback();
    }

    prevRecEnabled = sysState.params.autoRecEnabled;
    prevPlayEnabled = sysState.params.autoPlayEnabled;
}