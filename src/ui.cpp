#include "ui.h"
#include "constants.h"
#include "hw.h"
#include "patch_memory.h"

U8G2_SSD1305_128X32_ADAFRUIT_F_HW_I2C* u8g2;

void uiInit() {
    setOutMuxBit(DRST_BIT, LOW);
    delayMicroseconds(2);
    setOutMuxBit(DRST_BIT, HIGH);
    
    // Allocate u8g2 dynamically after heap is ready to prevent static initialization faults
    u8g2 = new U8G2_SSD1305_128X32_ADAFRUIT_F_HW_I2C(U8G2_R0);
    
    u8g2->begin();
    setOutMuxBit(DEN_BIT, HIGH);
    setOutMuxBit(KNOB_MODE, HIGH);
}

// Modify parameter value
void uiHandleKnobRotation(uint8_t knobIndex, int8_t direction) {
    MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(5));
    if (lock) {
        // Highlight the knob being turned (persists 500ms)
        sysState.highlightedKnob = knobIndex;
        sysState.highlightEndTime = millis() + 500;

        if (!sysState.menuMode) {
                // If not in menu mode, knob 0 controls master volume
                if (knobIndex == 0) {
                    int16_t vol = sysState.params.masterVol + direction;
                    if (vol < 0) vol = 0;
                    if (vol > 8) vol = 8;
                    sysState.params.masterVol = vol;
                }
            } else {
                // Change parameter based on knob index and active page
                switch (sysState.activePage) {
                    case PAGE_OSC:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.osc1WaveMorph + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 255) val = 255;
                            sysState.params.osc1WaveMorph = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.osc2Wave + direction;
                            if (val < 0) val = WAVEFORM_COUNT - 1;
                            if (val >= WAVEFORM_COUNT) val = 0;
                            sysState.params.osc2Wave = (WaveformType)val;
                        } else if (knobIndex == 2) {
                            int16_t val = sysState.params.mixOsc2 + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 100) val = 100;
                            sysState.params.mixOsc2 = val;
                        } else if (knobIndex == 3) {
                            int16_t val = sysState.params.osc2Detune + (direction * 2);
                            if (val < -50) val = -50;
                            if (val > 50) val = 50;
                            sysState.params.osc2Detune = val;
                        }
                        break;
                    case PAGE_OSC_EXT:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.subOscMix + (direction * 5);
                            if (val < 0) val = 0; if (val > 100) val = 100;
                            sysState.params.subOscMix = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.noiseMix + (direction * 5);
                            if (val < 0) val = 0; if (val > 100) val = 100;
                            sysState.params.noiseMix = val;
                        } else if (knobIndex == 2) {
                            int16_t val = sysState.params.ringModMix + (direction * 5);
                            if (val < 0) val = 0; if (val > 100) val = 100;
                            sysState.params.ringModMix = val;
                        } else if (knobIndex == 3) {
                            int16_t val = sysState.params.wavefold + (direction * 5);
                            if (val < 0) val = 0; if (val > 127) val = 127;
                            sysState.params.wavefold = val;
                        }
                        break;
                    case PAGE_FLT:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.filterCutoff + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.filterCutoff = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.filterRes + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.filterRes = val;
                        } else if (knobIndex == 2) {
                            int16_t val = sysState.params.filterEnvDepth + (direction * 4);
                            if (val < -64) val = -64;
                            if (val > 64) val = 64;
                            sysState.params.filterEnvDepth = val;
                        } else if (knobIndex == 3) {
                            int16_t val = sysState.params.filterType + direction;
                            if (val < 0) val = 3;
                            if (val > 3) val = 0;
                            sysState.params.filterType = val;
                        }
                        break;
                    case PAGE_FLT_MODEL:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.filterModel + direction;
                            if (val < 0) val = 2; // Wrap around to MS-20
                            if (val > 2) val = 0; // Wrap around to Standard
                            sysState.params.filterModel = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.filterDrive + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.filterDrive = val;
                        }
                        break;
                    case PAGE_ENV:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.envAttack + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.envAttack = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.envDecay + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.envDecay = val;
                        } else if (knobIndex == 2) {
                            int16_t val = sysState.params.envSustain + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.envSustain = val;
                        } else if (knobIndex == 3) {
                            int16_t val = sysState.params.envRelease + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.envRelease = val;
                        }
                        break;
                    case PAGE_MOD:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.lfoRate + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.lfoRate = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.lfoDepth + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.lfoDepth = val;
                        } else if (knobIndex == 3) {
                            int16_t val = sysState.params.glideTime + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.glideTime = val;
                        }
                        break;
                    case PAGE_MOD_ENV:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.modEnvAttack + (direction * 5);
                            if (val < 0) val = 0; if (val > 127) val = 127;
                            sysState.params.modEnvAttack = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.modEnvDecay + (direction * 5);
                            if (val < 0) val = 0; if (val > 127) val = 127;
                            sysState.params.modEnvDecay = val;
                        } else if (knobIndex == 2) {
                            int16_t val = sysState.params.modEnvAmount + (direction * 4);
                            if (val < -64) val = -64; if (val > 64) val = 64;
                            sysState.params.modEnvAmount = val;
                        } else if (knobIndex == 3) {
                            int16_t val = sysState.params.modEnvTarget + direction;
                            if (val < 0) val = 2; if (val > 2) val = 0;
                            sysState.params.modEnvTarget = val;
                        }
                        break;
                    case PAGE_MOD_EXT:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.shDepth + (direction * 5);
                            if (val < 0) val = 0; if (val > 127) val = 127;
                            sysState.params.shDepth = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.shTarget + direction;
                            if (val < 0) val = 1; if (val > 1) val = 0;
                            sysState.params.shTarget = val;
                        }
                        break;
                    case PAGE_FX:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.delayTime + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.delayTime = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.delayFeedback + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.delayFeedback = val;
                        } else if (knobIndex == 2) {
                            int16_t val = sysState.params.delayMix + (direction * 5);
                            if (val < 0) val = 0;
                            if (val > 127) val = 127;
                            sysState.params.delayMix = val;
                        } else if (knobIndex == 3) {
                            sysState.params.oscSync = (direction > 0);
                        }
                        break;
                    case PAGE_FX_EXT:
                        if (knobIndex == 0) {
                            int16_t val = sysState.params.chorusRate + (direction * 5);
                            if (val < 0) val = 0; if (val > 127) val = 127;
                            sysState.params.chorusRate = val;
                        } else if (knobIndex == 1) {
                            int16_t val = sysState.params.chorusDepth + (direction * 5);
                            if (val < 0) val = 0; if (val > 127) val = 127;
                            sysState.params.chorusDepth = val;
                        } else if (knobIndex == 2) {
                            int16_t val = sysState.params.chorusMix + (direction * 5);
                            if (val < 0) val = 0; if (val > 100) val = 100;
                            sysState.params.chorusMix = val;
                        } else if (knobIndex == 3) {
                            int16_t val = sysState.params.bitcrushDepth + direction;
                            if (val < 0) val = 7; if (val > 7) val = 0;
                            sysState.params.bitcrushDepth = val;
                        }
                        break;
                    case PAGE_PATCH:
                        if (knobIndex == 0) {
                            // Select patch slot (0-15)
                            int16_t val = sysState.currentPatchSlot + direction;
                            if (val < 0) val = 0;
                            if (val > 15) val = 15;
                            sysState.currentPatchSlot = val;
                            sysState.patchDirty = true;  // Mark as dirty when changing
                        } else if (knobIndex == 1) {
                            // Load patch (short press simulation via knob turn)
                            if (direction != 0) {
                                patchLoad(sysState.currentPatchSlot);
                                sysState.patchDirty = false;
                            }
                        } else if (knobIndex == 2) {
                            // Save patch
                            if (direction > 0) {
                                static char patchName[15];
                                sprintf(patchName, "Patch %02d    ", sysState.currentPatchSlot);
                                patchSave(sysState.currentPatchSlot, patchName);
                                sysState.patchDirty = false;
                            }
                        } else if (knobIndex == 3) {
                            // Initialize patch (reset to defaults)
                            if (direction > 0) {
                                patchLoadDefault();
                                sysState.patchDirty = true;
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
    }
}

void displayUpdateTask(void * pvParameters) {
    const TickType_t xFrequency = pdMS_TO_TICKS(100);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    static const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* pageNames[] = {"OSC", "OSC2", "FLT", "MODEL", "ENV", "MOD", "MENV", "S&H", "FX", "CHO", "PATCH"};

    while(1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        SystemState localState;
        {
            MutexGuard lock(sysState.mutex);
            if (lock) {
                localState = sysState;
            }
        }

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
            auto drawHighlight = [hasHighlight, highlightKnob](uint8_t knob, uint8_t x, uint8_t y) {
                if (hasHighlight && highlightKnob == knob) {
                    u8g2->drawTriangle(x, y-3, x+3, y+3, x-3, y+3);
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
                    const char* filterTypes[] = {"LP", "HP", "BP", "NOTCH"};
                    sprintf(buf, "Type: %s", filterTypes[localState.params.filterType]);
                    u8g2->print(buf);
                    
                    break;
                }
                case PAGE_FLT_MODEL: {
                    drawHighlight(0, 0, 16);
                    drawHighlight(1, 0, 24);
                    
                    u8g2->setCursor(8, 16);
                    const char* filterModels[] = {"STD", "MOOG", "MS20"};
                    sprintf(buf, "Mod: %s", filterModels[localState.params.filterModel]);
                    u8g2->print(buf);
                    
                    u8g2->setCursor(8, 24);
                    sprintf(buf, "Drv: %d", localState.params.filterDrive);
                    u8g2->print(buf);
                    
                    break;
                }
                case PAGE_ENV:
                    drawHighlight(0, 0, 16);
                    drawHighlight(1, 0, 24);
                    drawHighlight(2, 64, 16);
                    drawHighlight(3, 64, 24);
                    
                    u8g2->setCursor(8, 16);
                    sprintf(buf, "A: %d", localState.params.envAttack);
                    u8g2->print(buf);
                    u8g2->setCursor(8, 24);
                    sprintf(buf, "D: %d", localState.params.envDecay);
                    u8g2->print(buf);
                    u8g2->setCursor(72, 16);
                    sprintf(buf, "S: %d", localState.params.envSustain);
                    u8g2->print(buf);
                    u8g2->setCursor(72, 24);
                    sprintf(buf, "R: %d", localState.params.envRelease);
                    u8g2->print(buf);
                    break;
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
                case PAGE_MOD_ENV: {
                    drawHighlight(0, 0, 16);
                    drawHighlight(1, 0, 24);
                    drawHighlight(2, 64, 16);
                    drawHighlight(3, 64, 24);
                    
                    u8g2->setCursor(8, 16);
                    sprintf(buf, "A: %d", localState.params.modEnvAttack);
                    u8g2->print(buf);
                    u8g2->setCursor(8, 24);
                    sprintf(buf, "D: %d", localState.params.modEnvDecay);
                    u8g2->print(buf);
                    u8g2->setCursor(72, 16);
                    sprintf(buf, "Amt: %d", localState.params.modEnvAmount);
                    u8g2->print(buf);
                    u8g2->setCursor(72, 24);
                    const char* targets[] = {"PTCH", "FLT", "OSC2"};
                    sprintf(buf, "Tgt: %s", targets[localState.params.modEnvTarget]);
                    u8g2->print(buf);
                    break;
                }
                case PAGE_MOD_EXT: {
                    drawHighlight(0, 0, 16);
                    drawHighlight(1, 0, 24);
                    
                    u8g2->setCursor(8, 16);
                    sprintf(buf, "SH Dpth: %d", localState.params.shDepth);
                    u8g2->print(buf);
                    u8g2->setCursor(8, 24);
                    const char* shTargets[] = {"PTCH", "FLT"};
                    sprintf(buf, "SH Tgt: %s", shTargets[localState.params.shTarget]);
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
                case PAGE_FX_EXT: {
                    drawHighlight(0, 0, 16);
                    drawHighlight(1, 0, 24);
                    drawHighlight(2, 64, 16);
                    drawHighlight(3, 64, 24);
                    
                    u8g2->setCursor(8, 16);
                    sprintf(buf, "CRate: %d", localState.params.chorusRate);
                    u8g2->print(buf);
                    u8g2->setCursor(8, 24);
                    sprintf(buf, "CDepth: %d", localState.params.chorusDepth);
                    u8g2->print(buf);
                    u8g2->setCursor(72, 16);
                    sprintf(buf, "CMix: %d", localState.params.chorusMix);
                    u8g2->print(buf);
                    u8g2->setCursor(72, 24);
                    sprintf(buf, "Crush: %d", localState.params.bitcrushDepth);
                    u8g2->print(buf);
                    break;
                }
                case PAGE_PATCH: {
                    // Patch management page
                    drawHighlight(0, 0, 16);
                    drawHighlight(1, 0, 24);
                    drawHighlight(2, 64, 16);
                    drawHighlight(3, 64, 24);
                    
                    u8g2->setCursor(8, 16);
                    sprintf(buf, "Slot: %02d", localState.currentPatchSlot);
                    u8g2->print(buf);
                    
                    u8g2->setCursor(8, 24);
                    const char* patchName = patchGetName(localState.currentPatchSlot);
                    if (patchName[0] != '\0') {
                        sprintf(buf, "Name: %s", patchName);
                    } else {
                        sprintf(buf, "Name: (empty)");
                    }
                    u8g2->print(buf);
                    
                    u8g2->setCursor(72, 16);
                    sprintf(buf, "Load: K1");
                    u8g2->print(buf);
                    
                    u8g2->setCursor(72, 24);
                    sprintf(buf, "Save: K2");
                    u8g2->print(buf);
                    
                    u8g2->setCursor(8, 32);
                    sprintf(buf, "Init: K3  Dirty: %s", localState.patchDirty ? "Yes" : "No");
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
                    u8g2->setCursor(0, 20);
                    u8g2->print("-");
                } else {
                    // Build string of all pressed notes
                    char noteStr[48] = "";
                    size_t pos = 0;
                    for (uint8_t i = 0; i < numKeys && i < MAX_PRESSED_KEYS; i++) {
                        uint16_t key = localState.pressedKeys[i];
                        const char* note = notes[key % 12];
                        size_t len = strlen(note);
                        if (pos + len + 1 < sizeof(noteStr)) {
                            if (pos > 0) noteStr[pos++] = ' ';
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
                    u8g2->setCursor(0, 20);
                    u8g2->print(noteStr);
                }

                u8g2->setFont(u8g2_font_ncenB08_tr);
                u8g2->setCursor(0, 31);
                u8g2->print("VOL: ");
                u8g2->print(localState.params.masterVol);
                
                // Print CAN RX_Message
                u8g2->setFont(u8g2_font_5x7_tr);
                u8g2->setCursor(66, 30);
                u8g2->print((char) localState.RX_Message[0]);
                u8g2->print(localState.RX_Message[1]);
                u8g2->print(localState.RX_Message[2]);

                // 2. Right Side: Waveform Graph
                const uint8_t graphStartX = 45;
                const uint8_t graphEndX = 127;
                const uint8_t graphWidth = graphEndX - graphStartX;
                const uint8_t graphCenterY = 12;
                const uint8_t graphHeightAmp = 12; // +/- 12 pixels

                int lastY = -1;
                for (uint8_t x = 0; x < graphWidth; x++) {
                    // Calculate phase 0-255 based on x position (draws 2 full cycles for OSC1)
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
                        if (waveBase2 > 3) waveBase2 = 3;
                        uint8_t morphFract = (localState.params.osc1WaveMorph & 0x3F) << 2; // 0-255

                        int32_t s1a = uiGetWaveSample((WaveformType)waveBase1, phase1);
                        int32_t s1b = uiGetWaveSample((WaveformType)waveBase2, phase1);
                        sample1 = ((s1a * (255 - morphFract)) + (s1b * morphFract)) >> 8;
                    }
                    
                    int32_t sample2 = uiGetWaveSample(localState.params.osc2Wave, phase2);

                    // If WAVEFORM_OFF is selected, it returns 0 amplitude, so mixing logic still works
                    int32_t mix = localState.params.mixOsc2;
                    int32_t vout = ((sample1 * (100 - mix)) + (sample2 * mix)) / 100; // -128 to 127

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
                    sprintf(waveText, "M:%03d + %s", localState.params.osc1WaveMorph, waveformNames[localState.params.osc2Wave]);
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
                u8g2->drawHLine(0, 20, 128);  // Center line
                
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
                        if (waveBase2 > 3) waveBase2 = 3;
                        uint8_t morphFract = (localState.params.osc1WaveMorph & 0x3F) << 2; // 0-255

                        int32_t s1a = uiGetWaveSample((WaveformType)waveBase1, phase);
                        int32_t s1b = uiGetWaveSample((WaveformType)waveBase2, phase);
                        sample1 = ((s1a * (255 - morphFract)) + (s1b * morphFract)) >> 8;
                    }
                    int32_t mix = localState.params.mixOsc2;
                    int32_t sample2 = uiGetWaveSample(localState.params.osc2Wave, phase);
                    int32_t vout = ((sample1 * (100 - mix)) + (sample2 * mix)) / 100;
                    
                    int y = 20 - ((vout * 8) / 128);
                    if (y < 11) y = 11;
                    if (y > 29) y = 29;
                    
                    if (lastY != -1) {
                        u8g2->drawLine(x - 1, lastY, x, y);
                    }
                    lastY = y;
                }
                
                // Show frequency info
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
                uint8_t decayX = attackX + (localState.params.envDecay * (envW - attackX)) / 255;
                uint8_t sustainY = envY + envH - ((localState.params.envSustain * envH) / 255);
                uint8_t releaseX = decayX + (localState.params.envRelease * (envW - decayX)) / 255;
                if (releaseX > envX + envW) releaseX = envX + envW;
                
                // Draw envelope shape
                u8g2->drawPixel(envX, envY + envH);  // Start
                u8g2->drawLine(envX, envY + envH, attackX, envY);  // Attack
                u8g2->drawLine(attackX, envY, decayX, sustainY);  // Decay
                u8g2->drawLine(decayX, sustainY, releaseX, sustainY);  // Sustain
                u8g2->drawLine(releaseX, sustainY, envX + envW, envY + envH);  // Release
                
                // Show values
                u8g2->setFont(u8g2_font_5x7_tr);
                char buf[32];
                sprintf(buf, "A:%d D:%d S:%d R:%d", 
                    localState.params.envAttack,
                    localState.params.envDecay,
                    localState.params.envSustain,
                    localState.params.envRelease);
                u8g2->setCursor(0, 30);
                u8g2->print(buf);
            }
            
            // View mode indicator
            u8g2->setFont(u8g2_font_5x7_tr);
            const char* viewNames[] = {"PERF", "SCOPE", "ENV"};
            u8g2->setCursor(100, 8);
            u8g2->print(viewNames[localState.viewMode]);
        }
        
        u8g2->sendBuffer();
        digitalToggle(LED_BUILTIN);
    }
}
