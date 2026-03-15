#pragma once
#include "globals.h"
#include <Arduino.h>

// Patch memory configuration
#define PATCH_SLOTS 16
#define PATCH_MAGIC 0x50415443 // "PATC" in hex
#define PATCH_VERSION 1

// Flash storage addresses (STM32L432KC has 1KB EEPROM emulation area)
// We'll use the last pages of flash for patch storage
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 2048
#endif
#define PATCH_DATA_SIZE (sizeof(SynthParams) + 8) // Params + metadata
#define TOTAL_PATCH_DATA_SIZE (PATCH_SLOTS * PATCH_DATA_SIZE)

// Patch metadata stored with each preset
struct PatchMetadata {
  uint32_t magic;   // Magic number for validation
  uint8_t version;  // Data structure version
  uint8_t slot;     // Patch slot number (0-15)
  uint8_t name[14]; // Patch name (14 chars + null terminator)
  uint8_t reserved; // Alignment padding
};

// Complete patch structure (metadata + params)
struct Patch {
  PatchMetadata metadata;
  SynthParams params;
};

// Initialize patch memory system
void patchMemoryInit();

// Load a patch from flash into current params
// Returns true on success, false on failure (invalid slot or corrupted data)
bool patchLoad(uint8_t slot);

// Save current params to a patch slot in flash
// Returns true on success, false on failure
bool patchSave(uint8_t slot, const char *name = nullptr);

// Get patch name from a slot (without loading)
// Returns empty string if slot is invalid or corrupted
const char *patchGetName(uint8_t slot);

// Get current loaded patch slot
uint8_t patchGetCurrentSlot();

// Set current loaded patch slot (for UI tracking)
void patchSetCurrentSlot(uint8_t slot);

// Check if a patch slot contains valid data
bool patchIsValid(uint8_t slot);

// Erase all patches (factory reset)
void patchEraseAll();

// Get number of valid patches stored
uint8_t patchCountValid();

// Initialize default patch (factory init)
void patchLoadDefault();
