# VCP MIDI Testing Guide (Updated with Debug Mode)

## Quick Start for macOS

### Step 1: Flash the synth
```bash
# Using PlatformIO
pio run -t upload

# Or use the update script
./update.sh
```

### Step 2: Connect via USB
- Use your existing Micro USB cable
- Connect to the **CN1** port (the one with ST-Link)

### Step 3: Check serial port appears
```bash
ls -l /dev/tty.usbmodem*
```

You should see something like `/dev/tty.usbmodem12345678`.

**If nothing appears:**
- Try a different USB cable (some are charge-only)
- Check System Report → USB
- The port only appears when the synth is powered on

### Step 4: Enable Debug Mode on Synth

1. Press joystick to enter **Menu Mode**
2. Navigate right to **MIDI** page
3. Use knobs to set:
   - **Ch**: Your MIDI channel (1-16)
   - **Mode**: Poly/Omni/Mono
   - **VCP**: **On** (knob 2) - enables MIDI output
   - **DBG**: **On** (knob 3) - enables human-readable debug output

### Step 5: Test with Serial Monitor

Open Arduino IDE Serial Monitor or use terminal:

```bash
# Using screen (built into macOS)
screen /dev/tty.usbmodem* 31250

# Or using serial
screen /dev/tty.usbmodem* 38400
```

**Play a key on the synth.** You should see:
```
=== MIDI Debug Mode Enabled ===
Play keys to see Note On/Off messages
Turn knobs to see CC messages
================================
NOTE ON  Ch:1 Note:60 (C4) Vel:127
NOTE OFF Ch:1 Note:60 (C4) Vel:0
CC       Ch:1 CC:10 Value:64
```

**To exit screen:** Press `Ctrl+A`, then `K`, then `Y`

### Step 6: Test with SerialMIDI

1. Open **SerialMIDI** on macOS
2. Go to Preferences → Serial Ports
3. Select your `/dev/tty.usbmodem*` port
4. Set baud rate to **31250**
5. Enable the port
6. Open a MIDI test website: https://webmidi.io/testing/
7. Play keys on synth

## Debug Mode vs Binary Mode

| Mode | Use Case | Baud Rate | What You See |
|------|----------|-----------|--------------|
| **DBG: On** | Testing/debugging | Any (31250, 38400, 9600) | `NOTE ON Ch:1 Note:60` |
| **DBG: Off** | Production/SerialMIDI | 31250 | Binary MIDI bytes |

**For SerialMIDI to work:**
- **VCP: On** (required)
- **DBG: Off** (SerialMIDI needs binary MIDI)
- Baud rate: **31250**

**For testing with Serial Monitor:**
- **VCP: On** (required)
- **DBG: On** (easier to read)
- Baud rate: Any (31250, 38400, 9600)

## Troubleshooting

### No `/dev/tty.usbmodem*` port
- Cable issue - try different USB cable
- Port issue - try different USB port
- Driver issue - check System Report → USB → ST-Link

### Port exists but no data when playing keys
1. Check **VCP: On** in MIDI page
2. Check **DBG: On** to see readable output
3. Play a key while watching Serial Monitor

### SerialMIDI doesn't see MIDI device
1. Make sure **DBG: Off** (binary mode)
2. Make sure **VCP: On**
3. Baud rate must be **31250**
4. Quit and reopen SerialMIDI after changing settings

### Garbage characters in Serial Monitor
- This is normal in binary mode (DBG: Off)
- Turn **DBG: On** for readable output
- Or use a hex viewer to see MIDI bytes

### "Permission denied" error
```bash
# Check permissions
ls -l /dev/tty.usbmodem*

# May need to add user to dialout group
sudo dseditgroup -o edit -a $USER -t user dialout
# Then reboot
```

## Python Test Script

Save as `test_midi.py`:

```python
#!/usr/bin/env python3
import serial
import glob
import time

def find_stm32_port():
    ports = glob.glob('/dev/tty.usbmodem*')
    if ports:
        return ports[0]
    ports = glob.glob('/dev/tty.usbserial*')
    if ports:
        return ports[0]
    return None

port = find_stm32_port()
if not port:
    print("No STM32 port found!")
    exit(1)

print(f"Opening {port}...")
ser = serial.Serial(port, 31250, timeout=1)
time.sleep(0.5)

print("Play a key on the synth...")
print("Press Ctrl+C to exit")

try:
    while True:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            # Print as hex
            print(f"Received: {data.hex()}")
            
            # Check for MIDI messages
            for byte in data:
                if 0x80 <= byte <= 0x9F:
                    print(f"  -> Note message (channel {byte & 0x0F + 1})")
                elif 0xB0 <= byte <= 0xBF:
                    print(f"  -> CC message (channel {byte & 0x0F + 1})")
        time.sleep(0.05)
except KeyboardInterrupt:
    print("\nExiting...")
    ser.close()
```

Run it:
```bash
python3 test_midi.py
```

## What Each Knob Does (MIDI CC)

| Page | Knob | CC# | Range |
|------|------|-----|-------|
| OSC | 1 | 0 | Waveform (0-4) |
| OSC | 2 | 1 | Waveform (0-4) |
| OSC | 3 | 4 | Mix (0-100) |
| OSC | 4 | 2 | Detune (-50 to +50) |
| FLT | 1 | 10 | Cutoff (0-127) |
| FLT | 2 | 11 | Resonance (0-127) |
| FLT | 3 | 12 | Env Depth (-64 to +64) |
| ENV | 1 | 20 | Attack (0-127) |
| ENV | 2 | 21 | Decay (0-127) |
| ENV | 3 | 22 | Sustain (0-127) |
| ENV | 4 | 23 | Release (0-127) |
| MOD | 1 | 30 | LFO Rate (0-127) |
| MOD | 2 | 31 | LFO Depth (0-127) |
| MOD | 3 | 32 | LFO Target (0=Pitch, 1=Filter) |
| MOD | 4 | 33 | Glide (0-127) |
| FX | 1 | 40 | Delay Time (0-127) |
| FX | 2 | 41 | Delay Feedback (0-127) |
| FX | 3 | 42 | Delay Mix (0-127) |

## Next Steps

Once debug mode confirms data is flowing:

1. Turn **DBG: Off** for binary MIDI mode
2. Configure SerialMIDI with 31250 baud
3. Test with https://webmidi.io/testing/
4. Use with DAW (Ableton, Logic, etc.)
