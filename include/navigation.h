#pragma once
#include "globals.h"
#include <Arduino.h>

// View modes for the synthesizer display
enum class ViewMode : uint8_t { Performance = 0, Scope, Envelope, Menu, Count };

// Process joystick input and update menu/view state
// Call this from the scan task each cycle
// When pitchBendActive is true, Y-axis navigation is disabled (used for pitch bend instead)
void navUpdate(int16_t joyX, int16_t joyY, bool pitchBendActive = false);
