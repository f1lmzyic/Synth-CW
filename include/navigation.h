#pragma once
#include "globals.h"
#include <Arduino.h>

// Process joystick input and update page state
// Call this from the scan task each cycle
// When pitchBendActive is true, navigation is disabled (used for pitch bend instead)
void navUpdate(int16_t joyY, bool pitchBendActive = false);
