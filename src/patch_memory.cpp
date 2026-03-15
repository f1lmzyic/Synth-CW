#include "patch_memory.h"
#include <string.h>
#include <STM32FreeRTOS.h>

// Flash programming includes for STM32L4
#include <stm32l4xx_hal_flash.h>

// Current loaded patch slot
static uint8_t currentPatchSlot = 0;

// Buffer for patch data (must be 64-bit aligned for flash operations)
static uint8_t patchBuffer[PATCH_DATA_SIZE] __attribute__((aligned(8)));

// Mutex for flash operations
static SemaphoreHandle_t patchMutex = NULL;

// Default patch name
static const char defaultPatchName[] = "Init Patch    ";

// Calculate flash address for a given patch slot
// STM32L432KC: 256KB flash, last 2 pages (4KB) reserved for patches
static uint32_t getPatchAddress(uint8_t slot) {
    // Start from end of flash, work backwards
    // STM32L432KC has 256KB flash = 0x08000000 to 0x0803FFFF
    // Reserve last 4KB (2 pages) for patch storage
    uint32_t flashEnd = 0x0803FFFF;
    uint32_t patchAreaStart = flashEnd - TOTAL_PATCH_DATA_SIZE + 1;
    return patchAreaStart + (slot * PATCH_DATA_SIZE);
}

// Initialize patch memory system
void patchMemoryInit() {
    // Create mutex for thread-safe flash access
    patchMutex = xSemaphoreCreateMutex();
    if (patchMutex == NULL) {
        // Critical error - halt
        while(1) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            for(volatile int i=0; i<100000; i++);
        }
    }
    
    // Check if any valid patches exist
    if (patchCountValid() == 0) {
        // No valid patches - load defaults
        patchLoadDefault();
    } else {
        // Load first valid patch
        for (uint8_t i = 0; i < PATCH_SLOTS; i++) {
            if (patchIsValid(i)) {
                patchLoad(i);
                break;
            }
        }
    }
}

// Calculate CRC32 for data integrity check
static uint32_t calculateCRC32(const uint8_t* data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

// Check if a patch slot contains valid data
bool patchIsValid(uint8_t slot) {
    if (slot >= PATCH_SLOTS) return false;
    
    // Read metadata from flash
    PatchMetadata* metadata = (PatchMetadata*)getPatchAddress(slot);
    
    // Check magic number
    if (metadata->magic != PATCH_MAGIC) return false;
    
    // Check version
    if (metadata->version != PATCH_VERSION) return false;
    
    // Check slot consistency
    if (metadata->slot != slot) return false;
    
    return true;
}

// Load a patch from flash
bool patchLoad(uint8_t slot) {
    if (slot >= PATCH_SLOTS) return false;

    MutexGuard patchLock(patchMutex);
    if (!patchLock) return false;

    if (!patchIsValid(slot)) return false;

    Patch* patch = (Patch*)getPatchAddress(slot);

    SynthParams localParams;
    memcpy(&localParams, &patch->params, sizeof(SynthParams));

    MutexGuard sysLock(sysState.mutex);
    if (!sysLock) return false;

    sysState.params = localParams;
    currentPatchSlot = slot;
    return true;
}

// Wait for flash to be ready
static void waitForFlashReady() {
    uint32_t timeout = 1000000;
    while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY) != RESET) {
        timeout--;
        if (timeout == 0) {
            while(1) {
                digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
                for(volatile int i=0; i<50000; i++);
            }
        }
    }
}

// Save current params to flash
bool patchSave(uint8_t slot, const char* name) {
    if (slot >= PATCH_SLOTS) return false;

    Patch patch;
    memset(&patch, 0, sizeof(Patch));

    patch.metadata.magic = PATCH_MAGIC;
    patch.metadata.version = PATCH_VERSION;
    patch.metadata.slot = slot;

    // Set patch name
    if (name != nullptr) {
        strncpy((char*)patch.metadata.name, name, 14);
    } else {
        memcpy(patch.metadata.name, defaultPatchName, 14);
    }

    {
        MutexGuard sysLock(sysState.mutex);
        if (!sysLock) return false;
        memcpy(&patch.params, &sysState.params, sizeof(SynthParams));
    }
    
    // Calculate CRC for integrity (store in reserved field)
    uint32_t crc = calculateCRC32((uint8_t*)&patch.params, sizeof(SynthParams));
    patch.metadata.reserved = (uint8_t)(crc & 0xFF);

    MutexGuard patchLock(patchMutex);
    if (!patchLock) return false;

    // Flash programming sequence
    uint32_t address = getPatchAddress(slot);
    uint32_t dataSize = PATCH_DATA_SIZE;
    bool success = false;

    // Disable interrupts during flash operations
    __disable_irq();

    // Unlock flash
    HAL_FLASH_Unlock();

    // Clear flash status flags
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                          FLASH_FLAG_PGAERR | FLASH_FLAG_SIZERR | FLASH_FLAG_PGSERR);

    // Erase page(s) containing this patch
    // STM32L4 requires page erasure before programming
    FLASH_EraseInitTypeDef eraseInit;
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.Page = ((address - 0x08000000) / FLASH_PAGE_SIZE);
    eraseInit.NbPages = 1;

    uint32_t pageError;
    if (HAL_FLASHEx_Erase(&eraseInit, &pageError) == HAL_OK) {
        // Program flash byte by byte (or word by word)
        // Flash must be programmed from 0 to 1, never 1 to 0 (requires erase)
        uint8_t* data = (uint8_t*)&patch;
        success = true;

        for (uint32_t i = 0; i < dataSize; i += 8) {
            uint64_t word = *(uint64_t*)(data + i);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + i, word) != HAL_OK) {
                success = false;
                break;
            }
        }
    }

    HAL_FLASH_Lock();

    __enable_irq();

    return success;
}

// Get patch name from a slot
// NOTE: Returns pointer to static buffer - not thread-safe!
// Currently only called from displayUpdateTask, so this is safe in practice.
// If multi-threaded access is needed, caller should provide buffer.
const char* patchGetName(uint8_t slot) {
    static char nameBuffer[15];

    if (slot >= PATCH_SLOTS || !patchIsValid(slot)) {
        nameBuffer[0] = '\0';
        return nameBuffer;
    }

    Patch* patch = (Patch*)getPatchAddress(slot);
    strncpy(nameBuffer, (char*)patch->metadata.name, 14);
    nameBuffer[14] = '\0';

    return nameBuffer;
}

// Get current loaded patch slot
uint8_t patchGetCurrentSlot() {
    return currentPatchSlot;
}

// Set current loaded patch slot
void patchSetCurrentSlot(uint8_t slot) {
    currentPatchSlot = slot;
}

// Erase all patches
void patchEraseAll() {
    MutexGuard patchLock(patchMutex);
    if (!patchLock) return;

    __disable_irq();
    HAL_FLASH_Unlock();

    // Erase all patch pages
    FLASH_EraseInitTypeDef eraseInit;
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.Page = ((getPatchAddress(0) - 0x08000000) / FLASH_PAGE_SIZE);
    eraseInit.NbPages = (TOTAL_PATCH_DATA_SIZE / FLASH_PAGE_SIZE) + 1;

    uint32_t pageError;
    HAL_FLASHEx_Erase(&eraseInit, &pageError);

    HAL_FLASH_Lock();
    __enable_irq();
}

// Get number of valid patches
uint8_t patchCountValid() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < PATCH_SLOTS; i++) {
        if (patchIsValid(i)) {
            count++;
        }
    }
    return count;
}

// Load default patch
void patchLoadDefault() {
    MutexGuard lock(sysState.mutex);
    if (!lock) return;

    // Reset to default parameters (from dspInit)
    sysState.params.osc1WaveMorph = 0;
    sysState.params.osc2Wave = WAVEFORM_SQUARE;
    sysState.params.osc2Detune = 0;
    sysState.params.osc2Octave = -1;
    sysState.params.mixOsc2 = 50;

    sysState.params.subOscMix = 0;
    sysState.params.noiseMix = 0;
    sysState.params.ringModMix = 0;

    sysState.params.filterCutoff = 127;
    sysState.params.filterRes = 0;
    sysState.params.filterEnvDepth = 0;
    sysState.params.filterType = 0;
    sysState.params.filterModel = 0;
    sysState.params.filterDrive = 0;
    sysState.params.wavefold = 0;

    sysState.params.oscSync = false;

    sysState.params.envAttack = 10;
    sysState.params.envDecay = 40;
    sysState.params.envSustain = 64;
    sysState.params.envRelease = 40;

    sysState.params.modEnvAttack = 10;
    sysState.params.modEnvDecay = 40;
    sysState.params.modEnvAmount = 0;
    sysState.params.modEnvTarget = 0;

    sysState.params.lfoRate = 10;
    sysState.params.lfoDepth = 0;
    sysState.params.lfoTarget = 0;
    sysState.params.shDepth = 0;
    sysState.params.shTarget = 0;

    sysState.params.glideTime = 0;
    sysState.params.delayTime = 0;
    sysState.params.delayFeedback = 0;
    sysState.params.delayMix = 0;

    sysState.params.chorusRate = 0;
    sysState.params.chorusDepth = 0;
    sysState.params.chorusMix = 0;

    sysState.params.bitcrushDepth = 0;
    sysState.params.decimatorRate = 0;

    sysState.params.masterVol = 4;
    currentPatchSlot = 0;
}
