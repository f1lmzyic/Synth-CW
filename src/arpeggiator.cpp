#include "performance_features.h"

const char* arpModeName(uint8_t mode)
{
    switch (mode)
    {
    case ARP_UP:     return "UP";
    case ARP_DOWN:   return "DOWN";
    case ARP_UPDOWN: return "UPDN";
    case ARP_RANDOM: return "RAND";
    default:         return "UP";
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

void arpResetState()
{
    sysState.arpStepIndex = 0;
    sysState.arpDirectionUp = true;
    sysState.arpLastStepMs = millis();
}

void arpBuildOutputKeys(const uint16_t* inKeys, uint8_t inCount,
                        uint16_t* outKeys, uint8_t* outCount)
{
    *outCount = 0;

    if (inCount == 0)
    {
        arpResetState();
        return;
    }

    if (!sysState.params.arpEnabled)
    {
        for (uint8_t i = 0; i < inCount && i < MAX_PRESSED_KEYS; i++)
        {
            outKeys[i] = inKeys[i];
        }
        *outCount = inCount;
        return;
    }

    uint16_t pool[MAX_PRESSED_KEYS];
    uint8_t poolCount = 0;

    for (uint8_t oct = 0; oct <= sysState.params.arpOctaves; oct++)
    {
        for (uint8_t i = 0; i < inCount; i++)
        {
            if (poolCount < MAX_PRESSED_KEYS)
            {
                pool[poolCount++] = inKeys[i] + (12 * oct);
            }
        }
    }

    sortAscending(pool, poolCount);

    uint32_t now = millis();
    uint32_t stepIntervalMs = map(sysState.params.arpRate, 0, 127, 350, 60);

    if ((now - sysState.arpLastStepMs) >= stepIntervalMs)
    {
        sysState.arpLastStepMs = now;

        switch (sysState.params.arpMode)
        {
        case ARP_UP:
            sysState.arpStepIndex = (sysState.arpStepIndex + 1) % poolCount;
            break;

        case ARP_DOWN:
            if (sysState.arpStepIndex == 0)
                sysState.arpStepIndex = poolCount - 1;
            else
                sysState.arpStepIndex--;
            break;

        case ARP_UPDOWN:
            if (poolCount <= 1)
            {
                sysState.arpStepIndex = 0;
            }
            else if (sysState.arpDirectionUp)
            {
                if (sysState.arpStepIndex >= poolCount - 1)
                {
                    sysState.arpDirectionUp = false;
                    sysState.arpStepIndex--;
                }
                else
                {
                    sysState.arpStepIndex++;
                }
            }
            else
            {
                if (sysState.arpStepIndex == 0)
                {
                    sysState.arpDirectionUp = true;
                    sysState.arpStepIndex++;
                }
                else
                {
                    sysState.arpStepIndex--;
                }
            }
            break;

        case ARP_RANDOM:
            sysState.arpStepIndex = random(poolCount);
            break;

        default:
            break;
        }
    }

    outKeys[0] = pool[sysState.arpStepIndex % poolCount];
    *outCount = 1;
}