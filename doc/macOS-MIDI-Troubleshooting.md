# macOS MIDI Troubleshooting Guide

## Step 1: Verify Serial Port is Detected

Open Terminal and run:

```bash
# List all serial ports
ls -l /dev/tty.*

# Look for something like:
# /dev/tty.usbmodem12345678  (STM32 ST-Link)
# /dev/tty.usbserial-XXXX
```

**If you don't see a tty device:**
- Try a different USB cable (some are charge-only)
- Check System Report → USB → ST-Link
- The port only appears when the synth is powered and connected

## Step 2: Check What SerialMIDI Sees

1. Open SerialMIDI
2. Go to Preferences/Settings
3. Look for "Serial Ports" or "Input Sources"
4. You should see `/dev/tty.usbmodem*` listed

**If SerialMIDI doesn't see the port:**
- Quit SerialMIDI completely (Cmd+Q)
- Unplug/replug USB
- Reopen SerialMIDI
- Try adding the port manually

## Step 3: Verify Data is Being Sent

Open Arduino Serial Monitor or use screen:

```bash
# First, find your port name from Step 1
# Then connect at 31250 baud
screen /dev/tty.usbmodem12345678 31250
```

Now play a key on your synth. You should see **garbage characters** (this is expected - it's binary MIDI data displayed as text).

**If you see nothing:**
- VCP might be disabled in synth's MIDI page
- Try pressing keys while watching
- Check that synth is powered (LED blinking)

**To exit screen:** Press `Ctrl+A`, then `K`, then `Y`

## Step 4: Alternative - Use socat + Virtual MIDI

If SerialMIDI isn't working, try this manual approach:

```bash
# Install socat (if not installed)
brew install socat

# Create a virtual serial port pair
socat -d -d pty,raw,echo=0,link=/tmp/virtualmidi pty,raw,echo=0,link=/tmp/realserial &

# In another terminal, forward data from STM32 to virtual port
cat /dev/tty.usbmodem* > /tmp/realserial &

# Now use a MIDI app that can read from /tmp/virtualmidi
```

## Step 5: Use midiserver (Built-in macOS)

macOS has a built-in MIDI server. Create a configuration:

```bash
# Create MIDI configuration directory
mkdir -p ~/Library/Audio/MIDI\ Devices

# Check if midiserver is running
launchctl list | grep midiserver
```

## Step 6: Try Alternative Software

### Option A: Serial-USB MIDI Bridge (Python)

```bash
# Install Python MIDI library
pip3 install python-rtmidi pyserial

# Create a simple bridge script (see below)
```

### Option B: Hairless MIDI (if available for macOS)

Download from: https://projectgus.github.io/hairless-midiserial/

### Option C: TouchOSC Bridge

Free app that can bridge serial to MIDI.

## Step 7: Debug - Add Serial Print Output

The issue might be that MIDI data isn't being sent. Let me add debug output to verify.

## Common macOS Issues

### Issue 1: Permission Denied
```bash
# Check permissions
ls -l /dev/tty.usbmodem*

# If needed, add yourself to dialout group (may need reboot)
sudo dseditgroup -o edit -a $USER -t user dialout
```

### Issue 2: Baud Rate Mismatch
macOS serial ports can be picky about baud rates. Try:
- 31250 (standard MIDI)
- 38400 (common fallback)
- 9600 (debug mode)

### Issue 3: Port Already in Use
```bash
# Check if another process has the port
lsof | grep tty.usbmodem

# Kill any process using it
kill -9 <PID>
```

### Issue 4: CoreMIDI Cache
Sometimes CoreMIDI caches device list. Reset:
```bash
# Kill audio subsystem (will restart automatically)
sudo killall coreaudiod
```

## Quick Test Script

Save this as `test_midi.py`:

```python
#!/usr/bin/env python3
import serial
import glob
import time

def serial_ports():
    return glob.glob('/dev/tty.*')

print("Available serial ports:")
for port in serial_ports():
    print(f"  {port}")

# Try to open STM32 port
for port in serial_ports():
    if 'usbmodem' in port or 'usbserial' in port:
        print(f"\nTrying {port}...")
        try:
            ser = serial.Serial(port, 31250, timeout=1)
            print(f"Connected! Waiting for MIDI data...")
            print("Play a key on the synth...")
            
            start = time.time()
            while time.time() - start < 10:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting)
                    print(f"Received: {data.hex()}")
                    if data[0] >= 0x80 and data[0] <= 0x9F:
                        print("  -> MIDI Note message detected!")
            ser.close()
        except Exception as e:
            print(f"Error: {e}")
        break
else:
    print("No STM32 port found!")
```

Run it:
```bash
python3 test_midi.py
```

If you see "MIDI Note message detected!" → data is flowing, issue is with SerialMIDI configuration.

## Next Steps

If none of this works, I can:
1. Add a **debug mode** that sends text over Serial (easier to verify)
2. Change baud rate to something more macOS-friendly
3. Implement **USB MIDI Class** properly (requires hardware mod)
