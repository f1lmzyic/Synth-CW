#include "ui.h"
#include "constants.h"
#include "hw.h"

U8G2_SSD1305_128X32_ADAFRUIT_F_HW_I2C *u8g2;

void uiInit() {
  setOutMuxBit(DRST_BIT, LOW);
  delayMicroseconds(2);
  setOutMuxBit(DRST_BIT, HIGH);

  // Allocate u8g2 dynamically after heap is ready to prevent static
  u8g2 = new U8G2_SSD1305_128X32_ADAFRUIT_F_HW_I2C(U8G2_R0);

  u8g2->begin();
  setOutMuxBit(DEN_BIT, HIGH);
  setOutMuxBit(KNOB_MODE, HIGH);
}

int clamp(const int val, const int min, const int max) {
  if (val < min)
    return min;
  if (val > max)
    return max;
  return val;
}

int cycle(const int val, const int min, const int max) {
  if (val < min)
    return max;
  if (val > max)
    return min;
  return val;
}

// Modify parameter value
void uiHandleKnobRotation(uint8_t knobIndex, int8_t direction) {
  MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
  if (lock) {
    // Highlight the knob being turned (persists 500ms)
    sysState.highlightedKnob = knobIndex;
    sysState.highlightEndTime = millis() + 500;

    if (!sysState.menuMode) {
      // Performance mode knob assignments
      if (knobIndex == 0) {
        sysState.params.masterVol = clamp(sysState.params.masterVol + direction, 0, 8);
      } else if (knobIndex == 1) {
        sysState.octaveOffset = clamp(sysState.octaveOffset + direction, -2, 2);
      }
    } else {
      // Change parameter based on knob index and active page
      switch (sysState.activePage) {
      case PAGE_OSC:
        if (knobIndex == 0) {
          sysState.params.osc1WaveMorph = clamp(sysState.params.osc1WaveMorph + (direction * 5), 0, 255);
        } else if (knobIndex == 1) {
          sysState.params.osc2Wave = static_cast<WaveformType>(cycle(sysState.params.osc2Wave + direction, 0, WAVEFORM_COUNT - 1));
        } else if (knobIndex == 2) {
          sysState.params.mixOsc2 = clamp(sysState.params.mixOsc2 + direction * 5, 0, 100);
        } else if (knobIndex == 3) {
          sysState.params.osc2Detune = clamp(sysState.params.osc2Detune + direction * 2, -50, 50);
        }
        break;
      case PAGE_OSC_EXT:
        if (knobIndex == 0) {
          sysState.params.subOscMix = clamp(sysState.params.subOscMix + direction * 5, 0, 100);
        } else if (knobIndex == 1) {
          sysState.params.noiseMix = clamp(sysState.params.noiseMix + direction * 5, 0, 100);
        } else if (knobIndex == 2) {
          sysState.params.ringModMix = clamp(sysState.params.ringModMix + direction * 5, 0, 100);
        } else if (knobIndex == 3) {
          sysState.params.wavefold = clamp(sysState.params.wavefold + direction * 5, 0, 127);
        }
        break;
      case PAGE_FLT:
        if (knobIndex == 0) {
          sysState.params.filterCutoff = clamp(sysState.params.filterCutoff + direction * 5, 0, 127);
        } else if (knobIndex == 1) {
          sysState.params.filterRes = clamp(sysState.params.filterRes + direction * 5, 0, 127);
        } else if (knobIndex == 2) {
          sysState.params.filterEnvDepth = clamp(sysState.params.filterEnvDepth + direction * 4, -64, 64);
        } else if (knobIndex == 3) {
          sysState.params.filterType = cycle(sysState.params.filterType + direction, 0, 3);
        }
        break;
      case PAGE_ENV:
        if (knobIndex == 0) {
          sysState.params.envAttack = clamp(sysState.params.envAttack + direction * 5, 0, 127);
        } else if (knobIndex == 1) {
          sysState.params.envDecay = clamp(sysState.params.envDecay + direction * 5, 0, 127);
        } else if (knobIndex == 2) {
          sysState.params.envSustain = clamp(sysState.params.envSustain + direction * 5, 0, 127);
        } else if (knobIndex == 3) {
          sysState.params.envRelease = clamp(sysState.params.envRelease + direction * 5, 0, 127);
        }
        break;
      case PAGE_MOD:
        if (knobIndex == 0) {
          sysState.params.lfoRate = clamp(sysState.params.lfoRate + direction * 5, 0, 127);
        } else if (knobIndex == 1) {
          sysState.params.lfoDepth = clamp(sysState.params.lfoDepth + direction * 5, 0, 127);
        } else if (knobIndex == 3) {
          sysState.params.glideTime = clamp(sysState.params.glideTime + direction * 5, 0, 127);
        }
        break;
      case PAGE_FX:
        if (knobIndex == 0) {
          sysState.params.delayTime = clamp(sysState.params.delayTime + direction * 5, 0, 127);
        } else if (knobIndex == 1) {
          sysState.params.delayFeedback = clamp(sysState.params.delayFeedback + direction * 5, 0, 127);
        } else if (knobIndex == 2) {
          sysState.params.delayMix = clamp(sysState.params.delayMix + direction * 5, 0, 127);
        } else if (knobIndex == 3) {
          sysState.params.oscSync = (direction > 0);
        }
        break;
      default:
        break;
      }
    }
  }
}

void displayUpdateTask(void *pvParameters) {
  const TickType_t xFrequency = pdMS_TO_TICKS(100);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  static const char *notes[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                "F#", "G",  "G#", "A",  "A#", "B"};
  static const char *pageNames[] = {"OSC", "OSC2", "FLT", "ENV", "MOD", "FX"};
  static SystemState cachedState;

  while (1) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    {
      MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(10));
      if (lock) {
        cachedState = sysState;
      }
    }
    SystemState &localState = cachedState;

    u8g2->clearBuffer();
    u8g2->setFont(u8g2_font_5x7_tr);

    if (localState.menuMode) {
      // Page header with larger font
      u8g2->setFont(u8g2_font_ncenB08_tr);
      u8g2->setCursor(0, 8);
      u8g2->print(pageNames[localState.activePage]);

      // Scroll indicator (dots for pages)
      u8g2->setFont(u8g2_font_5x7_tr);
      for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == localState.activePage) {
          u8g2->drawBox(70 + (i * 5), 4, 4, 4);
        } else {
          u8g2->drawCircle(72 + (i * 5), 6, 2);
        }
      }

      // Check if highlight is still active
      bool hasHighlight = (millis() < localState.highlightEndTime);
      uint8_t highlightKnob = localState.highlightedKnob;

      // Helper to draw highlight indicator at position
      auto drawHighlight = [hasHighlight, highlightKnob](uint8_t knob,
                                                         uint8_t x, uint8_t y) {
        if (hasHighlight && highlightKnob == knob) {
          u8g2->drawTriangle(x, y - 3, x + 3, y + 3, x - 3, y + 3);
        }
      };

      // Parameters - more vertical space for readable labels
      u8g2->setFont(u8g2_font_5x7_tr);
      char buf[32];
      switch (localState.activePage) {
      case PAGE_OSC:
        // Draw highlight indicators (triangle to left of param)
        drawHighlight(0, 0, 16);
        drawHighlight(1, 0, 24);
        drawHighlight(2, 64, 16);
        drawHighlight(3, 64, 24);

        u8g2->setCursor(8, 16);
        sprintf(buf, "W1: %03d", localState.params.osc1WaveMorph);
        u8g2->print(buf);
        u8g2->setCursor(8, 24);
        sprintf(buf, "W2: %s", waveformNames[localState.params.osc2Wave]);
        u8g2->print(buf);
        u8g2->setCursor(72, 16);
        sprintf(buf, "Mix: %d", localState.params.mixOsc2);
        u8g2->print(buf);
        u8g2->setCursor(72, 24);
        sprintf(buf, "Det: %d", localState.params.osc2Detune);
        u8g2->print(buf);
        break;
      case PAGE_OSC_EXT:
        drawHighlight(0, 0, 16);
        drawHighlight(1, 0, 24);
        drawHighlight(2, 64, 16);
        drawHighlight(3, 64, 24);

        u8g2->setCursor(8, 16);
        sprintf(buf, "Sub: %d", localState.params.subOscMix);
        u8g2->print(buf);
        u8g2->setCursor(8, 24);
        sprintf(buf, "Nse: %d", localState.params.noiseMix);
        u8g2->print(buf);
        u8g2->setCursor(72, 16);
        sprintf(buf, "RM: %d", localState.params.ringModMix);
        u8g2->print(buf);
        u8g2->setCursor(72, 24);
        sprintf(buf, "Fold: %d", localState.params.wavefold);
        u8g2->print(buf);
        break;
      case PAGE_FLT: {
        drawHighlight(0, 0, 16);
        drawHighlight(1, 0, 24);
        drawHighlight(2, 0, 32);
        drawHighlight(3, 64, 16);

        u8g2->setCursor(8, 16);
        sprintf(buf, "Cut: %d", localState.params.filterCutoff);
        u8g2->print(buf);
        u8g2->setCursor(8, 24);
        sprintf(buf, "Res: %d", localState.params.filterRes);
        u8g2->print(buf);
        u8g2->setCursor(8, 32);
        sprintf(buf, "Env: %d", localState.params.filterEnvDepth);
        u8g2->print(buf);

        u8g2->setCursor(72, 16);
        const char *filterTypes[] = {"LP", "HP", "BP", "NOTCH"};
        sprintf(buf, "Type: %s", filterTypes[localState.params.filterType]);
        u8g2->print(buf);

        break;
      }
      case PAGE_ENV: {
        const uint8_t envX = 68;
        const uint8_t envY = 10;
        const uint8_t envW = 60;
        const uint8_t envH = 20;

        u8g2->drawFrame(envX, envY, envW, envH);

        // Calculate envelope points
        uint8_t attackX = envX + (localState.params.envAttack * envW) / 255;
        uint8_t decayX =
            attackX + (localState.params.envDecay * (envW - attackX)) / 255;
        uint8_t sustainY = envY + envH - ((localState.params.envSustain * envH) / 255);
        uint8_t releaseX =
            decayX + (localState.params.envRelease * (envW - decayX)) / 255;
        if (releaseX > envX + envW)
          releaseX = envX + envW;

        // Draw envelope shape
        u8g2->drawPixel(envX, envY + envH);
        u8g2->drawLine(envX, envY + envH, attackX, envY);
        u8g2->drawLine(attackX, envY, decayX, sustainY);
        u8g2->drawLine(decayX, sustainY, releaseX, sustainY);
        u8g2->drawLine(releaseX, sustainY, envX + envW, envY + envH);

        // Show values on left
        u8g2->setFont(u8g2_font_5x7_tr);
        drawHighlight(0, 0, 16);
        drawHighlight(1, 0, 24);
        drawHighlight(2, 32, 16);
        drawHighlight(3, 32, 24);

        u8g2->setCursor(8, 16);
        sprintf(buf, "A: %d", localState.params.envAttack);
        u8g2->print(buf);
        u8g2->setCursor(8, 24);
        sprintf(buf, "D: %d", localState.params.envDecay);
        u8g2->print(buf);
        u8g2->setCursor(40, 16);
        sprintf(buf, "S: %d", localState.params.envSustain);
        u8g2->print(buf);
        u8g2->setCursor(40, 24);
        sprintf(buf, "R: %d", localState.params.envRelease);
        u8g2->print(buf);
        break;
      }
      case PAGE_MOD: {
        drawHighlight(0, 0, 16);
        drawHighlight(1, 0, 24);
        drawHighlight(3, 64, 24);

        u8g2->setCursor(8, 16);
        sprintf(buf, "Rate: %d", localState.params.lfoRate);
        u8g2->print(buf);
        u8g2->setCursor(8, 24);
        sprintf(buf, "Depth: %d", localState.params.lfoDepth);
        u8g2->print(buf);
        u8g2->setCursor(72, 24);
        sprintf(buf, "Gld: %d", localState.params.glideTime);
        u8g2->print(buf);
        break;
      }
      case PAGE_FX: {
        drawHighlight(0, 0, 16);
        drawHighlight(1, 0, 24);
        drawHighlight(2, 0, 32);
        drawHighlight(3, 64, 16);

        u8g2->setCursor(8, 16);
        sprintf(buf, "Time: %d", localState.params.delayTime);
        u8g2->print(buf);
        u8g2->setCursor(8, 24);
        sprintf(buf, "Fbk: %d", localState.params.delayFeedback);
        u8g2->print(buf);
        u8g2->setCursor(8, 32);
        sprintf(buf, "Mix: %d", localState.params.delayMix);
        u8g2->print(buf);
        u8g2->setCursor(72, 16);
        sprintf(buf, "Sync: %s", localState.params.oscSync ? "On" : "Off");
        u8g2->print(buf);
        break;
      }
      default:
        break;
      }

    } else {
      // Performance Mode - different view modes
      if (localState.viewMode == 0) {
        // === View Mode 0: Performance (Note + Waveform) ===
        // 1. Left Side: Pressed notes and Volume
        uint8_t numKeys = localState.pressedKeyCount;
        if (numKeys == 0) {
          u8g2->setFont(u8g2_font_ncenB08_tr);
          u8g2->setCursor(0, 10);
          u8g2->print("-");
        } else {
          // Build string of all pressed notes
          char noteStr[48] = "";
          size_t pos = 0;
          for (uint8_t i = 0; i < numKeys && i < MAX_PRESSED_KEYS; i++) {
            uint16_t key = localState.pressedKeys[i];
            const char *note = notes[key % 12];
            size_t len = strlen(note);
            if (pos + len + 1 < sizeof(noteStr)) {
              if (pos > 0)
                noteStr[pos++] = ' ';
              strcpy(&noteStr[pos], note);
              pos += len;
            }
          }

          // Select font based on number of keys
          if (numKeys <= 2) {
            u8g2->setFont(u8g2_font_ncenB08_tr);
          } else if (numKeys <= 4) {
            u8g2->setFont(u8g2_font_5x8_tr);
          } else {
            u8g2->setFont(u8g2_font_4x6_tr);
          }
          u8g2->setCursor(0, 10);
          u8g2->print(noteStr);
        }

        u8g2->setFont(u8g2_font_ncenB08_tr);
        u8g2->setCursor(0, 20);
        u8g2->print("VOL: ");
        u8g2->print(localState.params.masterVol);
        u8g2->setCursor(0, 30);
        // Show the octave this specific board plays:
        //   base octave 4 + position relative to main board + user offset
        int thisOctave = 4 + ((int)localState.keyboardId - (int)localState.mainKeyboardId)
                           + localState.octaveOffset;
        char octBuf[12];
        sprintf(octBuf, "OCT: %d", thisOctave);
        u8g2->print(octBuf);

        // 2. Right Side: Waveform Graph
        const uint8_t graphStartX = 45;
        const uint8_t graphEndX = 127;
        const uint8_t graphWidth = graphEndX - graphStartX;
        const uint8_t graphCenterY = 12;
        const uint8_t graphHeightAmp = 12; // +/- 12 pixels

        int lastY = -1;
        for (uint8_t x = 0; x < graphWidth; x++) {
          // Calculate phase 0-255 based on x position (draws 2 full cycles for
          // OSC1)
          uint8_t phase1 = (x * 510) / graphWidth;

          // For OSC2, simulate detune and handle octave differences
          uint32_t phase2Temp = phase1;
          // Apply detune (visual approximation)
          phase2Temp += ((x * localState.params.osc2Detune) / 20);

          // Apply Octave mapping visually
          if (localState.params.osc2Octave > 0) {
            phase2Temp <<= localState.params.osc2Octave;
          } else if (localState.params.osc2Octave < 0) {
            phase2Temp >>= (-localState.params.osc2Octave);
          }
          uint8_t phase2 = (uint8_t)(phase2Temp & 0xFF);

          int32_t sample1 = 0;
          if (localState.params.osc1WaveMorph < 255) {
            uint8_t waveBase1 = localState.params.osc1WaveMorph >> 6; // 0-3
            uint8_t waveBase2 = waveBase1 + 1;
            if (waveBase2 > 3)
              waveBase2 = 3;
            uint8_t morphFract = (localState.params.osc1WaveMorph & 0x3F)
                                 << 2; // 0-255

            int32_t s1a = uiGetWaveSample((WaveformType)waveBase1, phase1);
            int32_t s1b = uiGetWaveSample((WaveformType)waveBase2, phase1);
            sample1 = ((s1a * (255 - morphFract)) + (s1b * morphFract)) >> 8;
          }

          int32_t sample2 = uiGetWaveSample(localState.params.osc2Wave, phase2);

          // If WAVEFORM_OFF is selected, it returns 0 amplitude, so mixing
          // logic still works
          int32_t mix = localState.params.mixOsc2;
          int32_t vout =
              ((sample1 * (100 - mix)) + (sample2 * mix)) / 100; // -128 to 127

          // Map to Y coordinate
          int y = graphCenterY - ((vout * graphHeightAmp) / 128);

          if (lastY != -1) {
            u8g2->drawLine(graphStartX + x - 1, lastY, graphStartX + x, y);
          } else {
            u8g2->drawPixel(graphStartX + x, y);
          }
          lastY = y;
        }

        // 3. Small text below graph
        u8g2->setFont(u8g2_font_5x7_tr);
        char waveText[32];

        if (localState.params.osc2Wave == WAVEFORM_OFF) {
          sprintf(waveText, "Morph: %03d", localState.params.osc1WaveMorph);
        } else if (localState.params.mixOsc2 == 100) {
          sprintf(waveText, "%s", waveformNames[localState.params.osc2Wave]);
        } else {
          sprintf(waveText, "M:%03d + %s", localState.params.osc1WaveMorph,
                  waveformNames[localState.params.osc2Wave]);
        }

        // Center the text under the graph
        uint8_t textWidth = u8g2->getStrWidth(waveText);
        uint8_t textX = graphStartX + (graphWidth / 2) - (textWidth / 2);
        u8g2->setCursor(textX, 31);
        u8g2->print(waveText);

      } else if (localState.viewMode == 1) {
        // === View Mode 1: Oscilloscope ===
        u8g2->setFont(u8g2_font_ncenB08_tr);
        u8g2->setCursor(0, 8);
        u8g2->print("SCOPE");

        // Draw oscilloscope grid
        u8g2->drawFrame(0, 10, 128, 20);
        u8g2->drawHLine(0, 20, 128); // Center line

        // Draw waveform (simulated real-time scope)
        static uint32_t scopePhase = 0;
        uint32_t scopeInc = 5 + (localState.params.lfoRate / 5);
        scopePhase += scopeInc;

        int lastY = -1;
        for (uint8_t x = 0; x < 128; x++) {
          uint8_t phase = ((x * 8) + scopePhase) & 0xFF;
          int32_t sample1 = 0;
          if (localState.params.osc1WaveMorph < 255) {
            uint8_t waveBase1 = localState.params.osc1WaveMorph >> 6; // 0-3
            uint8_t waveBase2 = waveBase1 + 1;
            if (waveBase2 > 3)
              waveBase2 = 3;
            uint8_t morphFract = (localState.params.osc1WaveMorph & 0x3F)
                                 << 2; // 0-255

            int32_t s1a = uiGetWaveSample((WaveformType)waveBase1, phase);
            int32_t s1b = uiGetWaveSample((WaveformType)waveBase2, phase);
            sample1 = ((s1a * (255 - morphFract)) + (s1b * morphFract)) >> 8;
          }
          int32_t mix = localState.params.mixOsc2;
          int32_t sample2 = uiGetWaveSample(localState.params.osc2Wave, phase);
          int32_t vout = ((sample1 * (100 - mix)) + (sample2 * mix)) / 100;

          int y = 20 - ((vout * 8) / 128);
          if (y < 11)
            y = 11;
          if (y > 29)
            y = 29;

          if (lastY != -1) {
            u8g2->drawLine(x - 1, lastY, x, y);
          }
          lastY = y;
        }

        u8g2->setFont(u8g2_font_5x7_tr);
        char buf[20];
        // Find first pressed key for display
        int scopeFirstKey = -1;
        if (localState.pressedKeyCount > 0) {
          scopeFirstKey = localState.pressedKeys[0];
        }
        if (scopeFirstKey >= 0) {
          sprintf(buf, "Key: %d", scopeFirstKey);
        } else {
          sprintf(buf, "Key: -");
        }
        u8g2->setCursor(0, 30);
        u8g2->print(buf);

        // Show pitch bend value
        int8_t bend = localState.displayPitchBend;
        bool pbEnabled = localState.pitchBendEnabled;
        if (pbEnabled) {
          if (bend > 0) {
            sprintf(buf, "Bend: +%d", bend);
          } else if (bend < 0) {
            sprintf(buf, "Bend: %d", bend);
          } else {
            sprintf(buf, "Bend: 0");
          }
        } else {
          sprintf(buf, "Bend: OFF");
        }
        u8g2->setCursor(60, 30);
        u8g2->print(buf);

      } else if (localState.viewMode == 2) {
        // === View Mode 2: Envelope Visualizer ===
        u8g2->setFont(u8g2_font_ncenB08_tr);
        u8g2->setCursor(0, 8);
        u8g2->print("ENV");

        // Draw envelope graph
        const uint8_t envX = 0;
        const uint8_t envY = 12;
        const uint8_t envW = 128;
        const uint8_t envH = 18;

        u8g2->drawFrame(envX, envY, envW, envH);

        // Calculate envelope points
        uint8_t attackX = envX + (localState.params.envAttack * envW) / 255;
        uint8_t decayX =
            attackX + (localState.params.envDecay * (envW - attackX)) / 255;
        uint8_t sustainY =
            envY + envH - ((localState.params.envSustain * envH) / 255);
        uint8_t releaseX =
            decayX + (localState.params.envRelease * (envW - decayX)) / 255;
        if (releaseX > envX + envW)
          releaseX = envX + envW;

        // Draw envelope shape
        u8g2->drawPixel(envX, envY + envH);                           // Start
        u8g2->drawLine(envX, envY + envH, attackX, envY);             // Attack
        u8g2->drawLine(attackX, envY, decayX, sustainY);              // Decay
        u8g2->drawLine(decayX, sustainY, releaseX, sustainY);         // Sustain
        u8g2->drawLine(releaseX, sustainY, envX + envW, envY + envH); // Release

        // Show values
        u8g2->setFont(u8g2_font_5x7_tr);
        char buf[32];
        sprintf(buf, "A:%d D:%d S:%d R:%d", localState.params.envAttack,
                localState.params.envDecay, localState.params.envSustain,
                localState.params.envRelease);
        u8g2->setCursor(0, 30);
        u8g2->print(buf);
      }

      // View mode indicator
      u8g2->setFont(u8g2_font_5x7_tr);
      const char *viewNames[] = {"PERF", "SCOPE", "ENV"};
      u8g2->setCursor(100, 8);
      u8g2->print(viewNames[localState.viewMode]);
    }

    u8g2->sendBuffer();
    digitalToggle(LED_BUILTIN);
  }
}
