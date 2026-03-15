#pragma once
#include "globals.h"
#include <Arduino.h>

// View modes for the synthesizer display
enum class ViewMode : uint8_t { Performance = 0, Scope, Envelope, Menu, Count };

// Process joystick input and update menu/view state
// Call this from the scan task each cycle
void navUpdate(int16_t joyX, int16_t joyY);
