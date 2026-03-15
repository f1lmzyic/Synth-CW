#include "performance_features.h"

static uint8_t getChordIntervals(uint8_t chordType, int8_t* intervals)
{
    switch (chordType)
    {
    case CHORD_MAJOR:
        intervals[0] = 0; intervals[1] = 4; intervals[2] = 7;
        return 3;
    case CHORD_MINOR:
        intervals[0] = 0; intervals[1] = 3; intervals[2] = 7;
        return 3;
    case CHORD_SUS2:
        intervals[0] = 0; intervals[1] = 2; intervals[2] = 7;
        return 3;
    case CHORD_SUS4:
        intervals[0] = 0; intervals[1] = 5; intervals[2] = 7;
        return 3;
    case CHORD_DOM7:
        intervals[0] = 0; intervals[1] = 4; intervals[2] = 7; intervals[3] = 10;
        return 4;
    case CHORD_MIN7:
        intervals[0] = 0; intervals[1] = 3; intervals[2] = 7; intervals[3] = 10;
        return 4;
    default:
        intervals[0] = 0; intervals[1] = 4; intervals[2] = 7;
        return 3;
    }
}

const char* chordTypeName(uint8_t type)
{
    switch (type)
    {
    case CHORD_MAJOR: return "MAJ";
    case CHORD_MINOR: return "MIN";
    case CHORD_SUS2:  return "SUS2";
    case CHORD_SUS4:  return "SUS4";
    case CHORD_DOM7:  return "7";
    case CHORD_MIN7:  return "M7";
    default:          return "MAJ";
    }
}

static void sortAscending(uint16_t* arr, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++)
    {
        for (uint8_t j = i + 1; j < count; j++)
        {
            if (arr[j] < arr[i])
            {
                uint16_t t = arr[i];
                arr[i] = arr[j];
                arr[j] = t;
            }
        }
    }
}

void chordExpandHeldKeys(const uint16_t* heldKeys, uint8_t heldCount,
                         uint16_t* outKeys, uint8_t* outCount)
{
    *outCount = 0;

    if (heldCount == 0)
        return;

    if (!sysState.params.chordEnabled)
    {
        for (uint8_t i = 0; i < heldCount && i < MAX_PRESSED_KEYS; i++)
        {
            outKeys[i] = heldKeys[i];
        }
        *outCount = heldCount;
        return;
    }

    int8_t intervals[4];
    uint8_t chordSize = getChordIntervals(sysState.params.chordType, intervals);

    for (uint8_t k = 0; k < heldCount; k++)
    {
        uint16_t root = heldKeys[k];
        uint16_t temp[4];

        for (uint8_t i = 0; i < chordSize; i++)
        {
            int16_t note = (int16_t)root + intervals[i];
            if (i > 0)
            {
                note += (12 * sysState.params.chordSpread);
            }
            temp[i] = (uint16_t)note;
        }

        // inversion: move bottom notes up one octave
        for (uint8_t inv = 0; inv < sysState.params.chordInversion && inv < chordSize; inv++)
        {
            temp[inv] += 12;
        }

        sortAscending(temp, chordSize);

        for (uint8_t i = 0; i < chordSize; i++)
        {
            if (*outCount < MAX_PRESSED_KEYS)
            {
                outKeys[*outCount] = temp[i];
                (*outCount)++;
            }
        }
    }

    sortAscending(outKeys, *outCount);
}