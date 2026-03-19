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

        // Change parameter based on knob index and active page
        switch (sysState.activePage) {
            case PAGE_PERFORMANCE:
                // Performance mode knob assignments
                if (knobIndex == 0) {
                    sysState.params.masterVol = clamp(sysState.params.masterVol + direction, 0, 8);
                } else if (knobIndex == 1) {
                    sysState.octaveOffset = clamp(sysState.octaveOffset + direction, -2, 2);
                }
                break;
            case PAGE_OSC:
                if (knobIndex == 0) {
                    sysState.params.osc1WaveMorph = clamp(sysState.params.osc1WaveMorph + (direction * 5), 0, 255);
                } else if (knobIndex == 1) {
                    sysState.params.osc2Wave = static_cast<WaveformType>(cycle(
                        sysState.params.osc2Wave + direction, 0, WAVEFORM_COUNT - 1));
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

static const char *notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *pageNames[] = {"PERF", "OSC", "OSC2", "FLT", "ENV", "MOD", "FX", "SCOPE"};
static const char *filterTypes[] = {"LP", "HP", "BP", "NOTCH"};

// Draw page header for menu pages
static void drawPageHeader(MenuPage page) {
    u8g2->setFont(u8g2_font_ncenB08_tr);
    u8g2->setCursor(0, 8);
    u8g2->print(pageNames[page]);
    u8g2->setFont(u8g2_font_5x7_tr);
}

// Draw knob highlight indicator
static void drawHighlight(bool active, uint8_t activeKnob, uint8_t knob, uint8_t x, uint8_t y) {
    if (active && activeKnob == knob) {
        u8g2->drawTriangle(x, y - 3, x + 3, y + 3, x - 3, y + 3);
    }
}

// Draw performance page with note display and waveform
static void drawPerformancePage(const SystemState &st) {
    uint8_t numKeys = st.pressedKeyCount;
    if (numKeys == 0) {
        u8g2->setFont(u8g2_font_ncenB08_tr);
        u8g2->setCursor(0, 10);
        u8g2->print("-");
    } else {
        char noteStr[32] = "";
        size_t pos = 0;
        for (uint8_t i = 0; i < numKeys && i < MAX_PRESSED_KEYS; i++) {
            const char *note = notes[st.pressedKeys[i] % 12];
            size_t len = strlen(note);
            if (pos + len + 1 < sizeof(noteStr)) {
                if (pos > 0) noteStr[pos++] = ' ';
                strcpy(&noteStr[pos], note);
                pos += len;
            }
        }
        u8g2->setFont(numKeys <= 2 ? u8g2_font_ncenB08_tr : numKeys <= 4 ? u8g2_font_5x8_tr : u8g2_font_4x6_tr);
        u8g2->setCursor(0, 10);
        u8g2->print(noteStr);
    }

    u8g2->setFont(u8g2_font_ncenB08_tr);
    u8g2->setCursor(0, 20);
    u8g2->print("VOL: ");
    u8g2->print(st.params.masterVol);

    int thisOctave = 4 + (int) st.keyboardId - (int) st.mainKeyboardId + st.octaveOffset;
    u8g2->setCursor(0, 30);
    u8g2->print("OCT: ");
    u8g2->print(thisOctave);

    // Waveform graph
    constexpr uint8_t GX = 45, GW = 82, GY = 12, GH = 12;
    int32_t mix = st.params.mixOsc2 + (st.params.mixOsc2 >> 2);
    uint8_t wb1 = st.params.osc1WaveMorph >> 6;
    uint8_t wb2 = (wb1 < 3) ? wb1 + 1 : 3;
    uint8_t mf = (st.params.osc1WaveMorph & 0x3F) << 2;

    int lastY = -1, lastX = GX;
    for (uint8_t x = 0; x < GW; x += 2) {
        uint8_t p1 = (x * 6) + (x >> 1);
        uint32_t p2t = p1 + ((x * st.params.osc2Detune) >> 4);
        if (st.params.osc2Octave > 0) p2t <<= st.params.osc2Octave;
        else if (st.params.osc2Octave < 0) p2t >>= -st.params.osc2Octave;
        uint8_t p2 = p2t & 0xFF;

        int32_t s1 = 0;
        if (st.params.osc1WaveMorph < 255) {
            s1 = ((uiGetWaveSample((WaveformType) wb1, p1) * (255 - mf)) +
                  (uiGetWaveSample((WaveformType) wb2, p1) * mf)) >> 8;
        }
        int32_t s2 = uiGetWaveSample(st.params.osc2Wave, p2);
        int y = GY - ((((s1 * (128 - mix)) + (s2 * mix)) >> 7) * GH >> 7);

        if (lastY != -1) u8g2->drawLine(lastX, lastY, GX + x, y);
        lastY = y;
        lastX = GX + x;
    }

    u8g2->setFont(u8g2_font_5x7_tr);
    char txt[24];
    if (st.params.osc2Wave == WAVEFORM_OFF)
        sprintf(txt, "Morph: %03d", st.params.osc1WaveMorph);
    else if (st.params.mixOsc2 == 100)
        sprintf(txt, "%s", waveformNames[st.params.osc2Wave]);
    else
        sprintf(txt, "M:%03d+%s", st.params.osc1WaveMorph, waveformNames[st.params.osc2Wave]);
    u8g2->setCursor(GX + (GW - u8g2->getStrWidth(txt)) / 2, 31);
    u8g2->print(txt);
}

// Draw oscilloscope page
static void drawScopePage(const SystemState &st) {
    static uint32_t scopePhase = 0;
    uint32_t scopeInc = 5 + (st.params.lfoRate / 5);
    scopePhase += scopeInc;

    // Header
    u8g2->setFont(u8g2_font_ncenB08_tr);
    u8g2->setCursor(0, 8);
    u8g2->print("SCOPE");

    // Draw oscilloscope frame and center line
    u8g2->drawFrame(0, 10, 128, 20);
    u8g2->drawHLine(0, 20, 128);

    // Draw waveform
    int lastY = -1;
    for (uint8_t x = 0; x < 128; x++) {
        uint8_t phase = ((x * 8) + scopePhase) & 0xFF;
        int32_t sample1 = 0;
        if (st.params.osc1WaveMorph < 255) {
            uint8_t waveBase1 = st.params.osc1WaveMorph >> 6;
            uint8_t waveBase2 = waveBase1 + 1;
            if (waveBase2 > 3) waveBase2 = 3;
            uint8_t morphFract = (st.params.osc1WaveMorph & 0x3F) << 2;

            int32_t s1a = uiGetWaveSample((WaveformType)waveBase1, phase);
            int32_t s1b = uiGetWaveSample((WaveformType)waveBase2, phase);
            sample1 = ((s1a * (255 - morphFract)) + (s1b * morphFract)) >> 8;
        }
        int32_t mix = st.params.mixOsc2;
        int32_t sample2 = uiGetWaveSample(st.params.osc2Wave, phase);
        int32_t vout = ((sample1 * (100 - mix)) + (sample2 * mix)) / 100;

        int y = 20 - ((vout * 8) / 128);
        if (y < 11) y = 11;
        if (y > 29) y = 29;

        if (lastY != -1) {
            u8g2->drawLine(x - 1, lastY, x, y);
        }
        lastY = y;
    }

    // Status line
    u8g2->setFont(u8g2_font_5x7_tr);
    char buf[20];

    // Find first pressed key for display
    int scopeFirstKey = -1;
    if (st.pressedKeyCount > 0) {
        scopeFirstKey = st.pressedKeys[0];
    }
    if (scopeFirstKey >= 0) {
        sprintf(buf, "Key: %d", scopeFirstKey);
    } else {
        sprintf(buf, "Key: -");
    }
    u8g2->setCursor(0, 30);
    u8g2->print(buf);

    // Show pitch bend value
    int8_t bend = st.displayPitchBend;
    bool pbEnabled = st.pitchBendEnabled;
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
}

// Draw envelope visualization
static void drawEnvelope(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                         uint8_t a, uint8_t d, uint8_t s, uint8_t r) {
    u8g2->drawFrame(x, y, w, h);
    uint8_t ax = x + (a * w) / 255;
    uint8_t dx = ax + (d * (w - (ax - x))) / 255;
    uint8_t sy = y + h - (s * h) / 255;
    uint8_t rx = dx + (r * (w - (dx - x))) / 255;
    if (rx > x + w) rx = x + w;

    u8g2->drawLine(x, y + h, ax, y);
    u8g2->drawLine(ax, y, dx, sy);
    u8g2->drawLine(dx, sy, rx, sy);
    u8g2->drawLine(rx, sy, x + w, y + h);
}

void displayUpdateTask(void *pvParameters) {
#ifndef TEST_DISPLAY
    const TickType_t xFrequency = pdMS_TO_TICKS(100);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
#endif

        SystemState st;
        {
            MutexGuard lock(sysState.mutex, pdMS_TO_TICKS(50));
            if (lock) st = sysState;
        }

        u8g2->clearBuffer();

        bool hl = millis() < st.highlightEndTime;
        uint8_t hk = st.highlightedKnob;
        char buf[24];

        switch (st.activePage) {
            case PAGE_PERFORMANCE:
                drawPerformancePage(st);
                break;

            case PAGE_OSC:
                drawPageHeader(st.activePage);
                drawHighlight(hl, hk, 0, 4, 13);
                drawHighlight(hl, hk, 1, 4, 21);
                drawHighlight(hl, hk, 2, 68, 13);
                drawHighlight(hl, hk, 3, 68, 21);
                u8g2->setCursor(8, 16);
                sprintf(buf, "W1: %03d", st.params.osc1WaveMorph);
                u8g2->print(buf);
                u8g2->setCursor(8, 24);
                sprintf(buf, "W2: %s", waveformNames[st.params.osc2Wave]);
                u8g2->print(buf);
                u8g2->setCursor(72, 16);
                sprintf(buf, "Mix: %d", st.params.mixOsc2);
                u8g2->print(buf);
                u8g2->setCursor(72, 24);
                sprintf(buf, "Det: %d", st.params.osc2Detune);
                u8g2->print(buf);
                break;

            case PAGE_OSC_EXT:
                drawPageHeader(st.activePage);
                drawHighlight(hl, hk, 0, 4, 13);
                drawHighlight(hl, hk, 1, 4, 21);
                drawHighlight(hl, hk, 2, 68, 13);
                drawHighlight(hl, hk, 3, 68, 21);
                u8g2->setCursor(8, 16);
                sprintf(buf, "Sub: %d", st.params.subOscMix);
                u8g2->print(buf);
                u8g2->setCursor(8, 24);
                sprintf(buf, "Nse: %d", st.params.noiseMix);
                u8g2->print(buf);
                u8g2->setCursor(72, 16);
                sprintf(buf, "RM: %d", st.params.ringModMix);
                u8g2->print(buf);
                u8g2->setCursor(72, 24);
                sprintf(buf, "Fold: %d", st.params.wavefold);
                u8g2->print(buf);
                break;

            case PAGE_FLT:
                drawPageHeader(st.activePage);
                drawHighlight(hl, hk, 0, 4, 13);
                drawHighlight(hl, hk, 1, 4, 21);
                drawHighlight(hl, hk, 2, 4, 29);
                drawHighlight(hl, hk, 3, 68, 13);
                u8g2->setCursor(8, 16);
                sprintf(buf, "Cut: %d", st.params.filterCutoff);
                u8g2->print(buf);
                u8g2->setCursor(8, 24);
                sprintf(buf, "Res: %d", st.params.filterRes);
                u8g2->print(buf);
                u8g2->setCursor(8, 32);
                sprintf(buf, "Env: %d", st.params.filterEnvDepth);
                u8g2->print(buf);
                u8g2->setCursor(72, 16);
                sprintf(buf, "Type: %s", filterTypes[st.params.filterType]);
                u8g2->print(buf);
                break;

            case PAGE_ENV:
                drawPageHeader(st.activePage);
                drawEnvelope(68, 10, 60, 20, st.params.envAttack, st.params.envDecay,
                             st.params.envSustain, st.params.envRelease);
                drawHighlight(hl, hk, 0, 4, 13);
                drawHighlight(hl, hk, 1, 4, 21);
                drawHighlight(hl, hk, 2, 36, 13);
                drawHighlight(hl, hk, 3, 36, 21);
                u8g2->setCursor(8, 16);
                sprintf(buf, "A: %d", st.params.envAttack);
                u8g2->print(buf);
                u8g2->setCursor(8, 24);
                sprintf(buf, "D: %d", st.params.envDecay);
                u8g2->print(buf);
                u8g2->setCursor(40, 16);
                sprintf(buf, "S: %d", st.params.envSustain);
                u8g2->print(buf);
                u8g2->setCursor(40, 24);
                sprintf(buf, "R: %d", st.params.envRelease);
                u8g2->print(buf);
                break;

            case PAGE_MOD:
                drawPageHeader(st.activePage);
                drawHighlight(hl, hk, 0, 4, 13);
                drawHighlight(hl, hk, 1, 4, 21);
                drawHighlight(hl, hk, 3, 68, 21);
                u8g2->setCursor(8, 16);
                sprintf(buf, "Rate: %d", st.params.lfoRate);
                u8g2->print(buf);
                u8g2->setCursor(8, 24);
                sprintf(buf, "Depth: %d", st.params.lfoDepth);
                u8g2->print(buf);
                u8g2->setCursor(72, 24);
                sprintf(buf, "Gld: %d", st.params.glideTime);
                u8g2->print(buf);
                break;

            case PAGE_FX:
                drawPageHeader(st.activePage);
                drawHighlight(hl, hk, 0, 4, 13);
                drawHighlight(hl, hk, 1, 4, 21);
                drawHighlight(hl, hk, 2, 4, 29);
                drawHighlight(hl, hk, 3, 68, 13);
                u8g2->setCursor(8, 16);
                sprintf(buf, "Time: %d", st.params.delayTime);
                u8g2->print(buf);
                u8g2->setCursor(8, 24);
                sprintf(buf, "Fbk: %d", st.params.delayFeedback);
                u8g2->print(buf);
                u8g2->setCursor(8, 32);
                sprintf(buf, "Mix: %d", st.params.delayMix);
                u8g2->print(buf);
                u8g2->setCursor(72, 16);
                sprintf(buf, "Sync: %s", st.params.oscSync ? "On" : "Off");
                u8g2->print(buf);
                break;

            case PAGE_SCOPE:
                drawScopePage(st);
                break;

            default:
                break;
        }

        u8g2->sendBuffer();
        digitalToggle(LED_BUILTIN);
#ifndef TEST_DISPLAY
    }
#endif
}
