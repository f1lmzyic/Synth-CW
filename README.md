# ES-synth-starter

STM32-based music synthesizer with real-time key scanning, audio generation, and FreeRTOS multitasking.

## Features Implemented

### Core Functionality
- **12-Key Musical Keyboard**: Scans key matrix (rows 0-2) to detect key presses
- **Sawtooth Wave Generation**: 22kHz sample rate using hardware timer (TIM1) interrupt
- **Real-time Audio Output**: Phase accumulator with 32-bit precision for accurate frequency generation
- **Equal Temperament Tuning**: Based on 440Hz reference for note A

### Threading & Synchronization (FreeRTOS)
- **scanKeysTask**: Scans key matrix every 20ms, handles knob decoding, updates note frequency
  - Priority: 2 (higher)
  - Stack: 128 words (512 bytes)
- **displayUpdateTask**: Updates OLED display every 100ms, shows current note and volume
  - Priority: 1 (lower)
  - Stack: 128 words (512 bytes)
- **Mutex Protection**: `sysState.mutex` protects shared data between threads
- **Atomic Operations**: ISR-safe access to `currentStepSize` and `knobRotation`

### Knob Control System
- **Knob 3 Decoding**: Quadrature encoder state machine
  - Sampled from row 3, columns 0-1
  - Handles normal and "impossible" transitions
  - Remembers last direction for missed states
- **Volume Control**: 0-8 range with logarithmic taper
  - Implemented in `sampleISR()` using right-shift: `Vout >> (8 - rotation)`
  - Atomic access for ISR safety
  - Display shows current volume level

### Audio Features
- **Logarithmic Volume**: Perceived loudness increases linearly with knob position
- **Real-time Processing**: Phase accumulator updates at 22kHz in hardware timer ISR
- **DC Offset Compensation**: Signal centered at 1.65V (midpoint of 0-3.3V range)

### Testing & Profiling
- **Execution Time Measurement**: Preprocessor-based testing framework
  - Define `TEST_SCANKEYS` to measure task execution time
  - Uses `micros()` for microsecond precision
  - Runs 32 iterations for averaging
  - Reports total and average execution time
- **Test Mode Features**:
  - `DISABLE_THREADS`: Skip normal FreeRTOS scheduler
  - `DISABLE_ISRS`: Disable interrupts during testing
  - Non-blocking task execution for isolated measurements

## Quick Start

1. **Hardware Setup**: Connect ST NUCLEO-L432KC to StackSynth module via USB
2. **Software**: Open in PlatformIO/VS Code
3. **Libraries Required**:
   - U8g2 (display driver)
   - STM32duino FreeRTOS
4. **Build & Upload**: Use PlatformIO upload button
5. **Serial Monitor**: Open at 9600 baud for debug output

## Usage

- **Play Notes**: Press keys C through B (12-key octave)
- **Adjust Volume**: Rotate Knob 3 (rightmost knob)
  - Clockwise: Increase volume
  - Counter-clockwise: Decrease volume
- **View Display**: Shows current note and volume level (0-8)

## Documentation

- [Lab Part 1](doc/LabPart1.md) - Basic key scanning and audio generation
- [Lab Part 2](doc/LabPart2.md) - Mutex, knobs, CAN bus, execution time measurement
- [Handshaking and auto-detection](doc/handshaking.md)
- [Double buffering of audio samples](doc/doubleBuffer.md)

## Hardware Specifications

- **MCU**: STM32L432KC (ARM Cortex-M4)
- **Display**: SSD1305 128x32 OLED
- **Audio**: 22kHz sample rate, 8-bit resolution
- **Key Matrix**: 3 rows x 4 columns (scanned)
- **Knobs**: Quadrature encoders via matrix

## Project Structure

```
src/
  main.cpp              # Main application code
  
doc/
  LabPart1.md           # Part 1 lab instructions
  LabPart2.md           # Part 2 lab instructions
  handshaking.md        # Module handshaking protocol
  doubleBuffer.md       # Audio buffering techniques
  StackSynth-v1.pdf     # V1.1 schematic
  StackSynth-v2.pdf     # V2.1 schematic
```

## Testing

To measure execution time:
```cpp
// Uncomment in main.cpp:
#define TEST_SCANKEYS
```

This will:
1. Disable normal FreeRTOS threads
2. Run scanKeysTask 32 times
3. Output total and average execution time via Serial
4. Halt with blinking LED

## Technical Details

### FreeRTOS Configuration
- Preemptive scheduling enabled
- Time slice: 1ms
- Mutex with priority inheritance for synchronization
- Stack overflow checking enabled

### Audio Processing
- **Phase Accumulator**: 32-bit for frequency accuracy
- **Step Size Formula**: `S = (2^32 * f) / 22000`
- **Volume Scaling**: Arithmetic right shift for signed values
- **Frequency Range**: C (261.63Hz) to B (493.88Hz)

### Key Matrix Scanning
- Rows selected via 3-to-8 decoder (RA0-RA2)
- Columns read as digital inputs (C0-C3)
- 3µs settling delay between row select and column read
- Active-low logic (0 = key pressed)

## License

This project is for educational purposes as part of Embedded Systems coursework.
