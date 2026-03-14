---
source: Electronics engineering best practices
library: GPIO Matrix Scanning
package: stm32-keyboard-matrix
topic: Key matrix limitations, ghosting, and N-key rollover
fetched: 2026-03-13T00:00:00Z
official_docs: N/A
---

# STM32 GPIO Matrix Scanning Best Practices

## Key Matrix Fundamentals

### Basic Matrix Operation
- Keys arranged in rows × columns grid
- MCU scans by: driving rows, reading columns (or vice versa)
- **Scan rate requirement:** >1kHz for musical keyboard response

### Matrix Size Limitations
| GPIO Count | Max Keys (no diodes) | Max Keys (with diodes) |
|------------|---------------------|------------------------|
| 8 GPIO | 16 keys (4×4) | 16 keys (4×4) |
| 16 GPIO | 64 keys (8×8) | 64 keys (8×8) |
| 20 GPIO | 100 keys (10×10) | 100 keys (10×10) |
| 32 GPIO | 256 keys (16×16) | 256 keys (16×16) |

**Note:** STM32F103 typically has 20-50 usable GPIO depending on package

---

## Ghosting and Phantom Key Issues

### What is Ghosting?
**Ghosting** occurs when pressing multiple keys creates a false "phantom" key detection at an unpressed key location.

### Ghosting Mechanism
```
    C1    C2    C3
R1  [K1]  [K2]  [K3]
R2  [K4]  [K5]  [K6]
R3  [K7]  [K8]  [K9]
```

**Example ghosting scenario:**
1. Press K1 (R1-C1), K2 (R1-C2), K4 (R2-C1) simultaneously
2. Current path: R1→C1→R2→C2→R1 creates loop
3. MCU detects K5 (R2-C2) as pressed even though it's not!

### When Ghosting Occurs
- **3-key ghosting:** Any 3 keys forming an "L" shape
- **4-key ghosting:** Any 4 keys forming a rectangle (all corners pressed)

### Ghosting Impact on MIDI
- False Note On messages sent for phantom keys
- Note Off never sent (key was never "really" pressed)
- **Result: Indefinite note playback**

---

## Diode Requirements for N-Key Rollover

### What is N-Key Rollover (NKRO)?
- **2KRO:** Maximum 2 simultaneous keys detected correctly
- **6KRO:** Maximum 6 simultaneous keys (standard for musical keyboards)
- **NKRO:** All keys detected regardless of how many pressed simultaneously

### Diode Solution
**Each key needs a series diode** to prevent current backflow:

```
    C1    C2    C3
R1  >[K1] >[K2] >[K3]
R2  >[K4] >[K5] >[K6]
R3  >[K7] >[K8] >[K9]
```

### Diode Specifications
| Parameter | Minimum | Recommended |
|-----------|---------|-------------|
| Forward Current | 20mA | 100mA |
| Reverse Voltage | 5V | 20V |
| Forward Voltage | - | <0.7V (Schottky preferred) |
| Switching Speed | - | Fast recovery |

### Recommended Diodes
- **1N4148:** Standard signal diode, adequate for most keyboards
- **BAT54:** Schottky diode, lower forward voltage drop
- **BAT54S:** Dual Schottky in SOT-23 package (space saving)

### Diode Orientation
```
Row-driven matrix (rows output, columns input):
- Diode anode → Row line
- Diode cathode → Key switch → Column line

Column-driven matrix (columns output, rows input):
- Diode anode → Column line  
- Diode cathode → Key switch → Row line
```

**CRITICAL:** All diodes must be oriented in the SAME direction

---

## STM32 GPIO Scanning Implementation

### Basic Scanning Algorithm
```c
void scanMatrix(void) {
    for (int row = 0; row < NUM_ROWS; row++) {
        // Drive current row low, others high
        GPIO_WriteBit(ROW_PORT, ALL_ROWS, Bit_SET);
        GPIO_WriteBit(ROW_PORT, ROW_PIN[row], Bit_RESET);
        
        // Wait for signal stabilization
        __NOP(); __NOP(); __NOP();
        
        // Read all columns
        for (int col = 0; col < NUM_COLS; col++) {
            if (GPIO_ReadInputDataBit(COL_PORT, COL_PIN[col]) == Bit_RESET) {
                // Key pressed at (row, col)
                handleKeyPress(row, col);
            }
        }
    }
    
    // Return all rows to high state
    GPIO_WriteBit(ROW_PORT, ALL_ROWS, Bit_SET);
}
```

### Scan Rate Requirements
- **Minimum:** 500 Hz (2ms per full scan)
- **Recommended:** 1000 Hz (1ms per full scan)
- **High performance:** 2000+ Hz (0.5ms per full scan)

### Debouncing Requirements
- **Mechanical switches:** 5-20ms debounce time
- **Conductive rubber:** 10-30ms debounce time
- **Implementation:** Software timer or counter-based

```c
// Simple debounce implementation
if (keyPressed[row][col]) {
    debounceCounter[row][col]++;
    if (debounceCounter[row][col] > DEBOUNCE_THRESHOLD) {
        if (!keyState[row][col]) {
            keyState[row][col] = true;
            sendNoteOn(row, col);
        }
    }
} else {
    debounceCounter[row][col] = 0;
    if (keyState[row][col]) {
        keyState[row][col] = false;
        sendNoteOff(row, col);
    }
}
```

---

## Common Matrix Scanning Bugs

### Bug 1: Insufficient Scan Rate
**Symptom:** Missed key presses during fast playing
**Cause:** Scan rate < key press speed
**Fix:** Increase scan frequency, reduce debounce time

### Bug 2: GPIO Mode Misconfiguration
**Symptom:** Unreliable key detection
**Cause:** Input pins not configured with pull-ups
**Fix:** Enable internal pull-up resistors on input pins

```c
// Correct GPIO configuration
GPIO_InitStructure.GPIO_Pin = COL_PINS;
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // Input Pull-Up!
GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
GPIO_Init(COL_PORT, &GPIO_InitStructure);
```

### Bug 3: Blocking in Scan Loop
**Symptom:** USB buffer overflow, missed keys
**Cause:** USB send operations blocking the scan loop
**Fix:** Queue key events, send USB messages separately

### Bug 4: No Diodes with >2 Key Presses
**Symptom:** Phantom notes, indefinite playback
**Cause:** Ghosting from missing diodes
**Fix:** Add diodes to EVERY key for 6KRO or NKRO

---

## N-Key Rollover vs USB Limitations

### Important Distinction
- **Matrix NKRO:** Hardware can detect all simultaneous presses
- **USB NKRO:** USB can transmit all simultaneous presses

### USB HID Keyboard Limitations
- Standard USB HID Keyboard: 6KRO maximum (by spec)
- USB MIDI: No inherent limit (limited by bandwidth)

### USB MIDI Advantage for Keyboards
```
USB MIDI Event: 4 bytes
- Cable Number (4 bits)
- Code Index Number (4 bits)  
- MIDI Status Byte (8 bits)
- MIDI Data Bytes (8-16 bits)

Bandwidth: ~3000 events/second at Full Speed
For 88-key keyboard with max velocity changes:
- Worst case: 88 Note On + 88 Note Off = 176 events
- Well within USB MIDI capacity
```

### Why 6+ Keys Still Fail (Even with Diodes)
1. Matrix correctly detects all keys (with diodes)
2. Scan loop generates Note On events
3. **USB send blocks waiting for buffer space**
4. Scan loop stops processing
5. Key release never detected
6. Note Off never sent

**This is a USB buffer issue, NOT a matrix issue!**

---

## Recommendations for Reliable Matrix Scanning

1. **Always use diodes** for musical keyboard applications
2. **Scan at ≥1kHz** for responsive feel
3. **Use interrupt-driven scanning** if possible
4. **Implement proper debouncing** (5-20ms)
5. **Queue key events** instead of sending USB directly
6. **Monitor USB buffer status** before sending
7. **Use FreeRTOS tasks** to separate scanning from USB
