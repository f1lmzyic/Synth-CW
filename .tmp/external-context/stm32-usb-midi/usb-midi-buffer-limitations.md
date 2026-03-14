---
source: GitHub - USBComposite_stm32f1 Issue #135 + Repository Documentation
library: USBComposite_stm32f1
package: stm32f1-usb-midi
topic: USB MIDI buffer overflow and limitations
fetched: 2026-03-13T00:00:00Z
official_docs: https://github.com/arpruss/USBComposite_stm32f1
---

# STM32 USB MIDI Buffer Limitations

## Critical Issue: Buffer Overflow Causes Device Freeze

**Issue #135** (Feb 25, 2025): "Feature Request: USB Buffer Management/Flush Functionality for MIDI Component"

### Problem Description
Continuous sending of MIDI messages without the host reading them causes the STM32 to **freeze after approximately 40 seconds**, likely due to buffer overflow.

### Root Cause
- The USB MIDI implementation uses a **fixed-size hardware buffer**
- When the host doesn't read data fast enough, the buffer fills up
- Once full, subsequent `sendNoteOff()` or `sendNoteOn()` calls block indefinitely
- This causes the main loop to hang, preventing key release detection

### Key Technical Details from Issue

```cpp
// User's implementation showing the problem
void loop() {
    midi.poll();
    
    // BUG: Continuous sending without host reading
    // will eventually fill the buffer and cause freeze after ~40 seconds
    midi.sendNoteOff(0, 0, 127);
    
    // Connection monitoring via Active Sensing timing
    bool connected = midi.checkConnection();
}
```

### Detection Method
The issue can be detected by measuring send time for Active Sensing messages:
- Quick send (<1ms) = buffer has space, connection active
- Slow send (>5ms) = buffer is full, host not reading

---

## Hardware Buffer Memory Limitations

### Total Available Buffer Memory
**320 bytes** of hardware buffer memory available after endpoint 0

### Default Buffer Allocation
| Component | Default Size | Notes |
|-----------|-------------|-------|
| USB Serial | 144 bytes | - |
| USB HID | 64 bytes | - |
| USB Mass Storage | 128 bytes | - |
| **USB MIDI** | **128 bytes** | **Critical for this issue** |
| XBox360 Controller | 64 bytes | - |

### USB MIDI Buffer Capacity
- **Default TX/RX packet size: 64 bytes** (maximum)
- **Total MIDI buffer: 128 bytes**
- **MIDI Event Packet: 4 bytes** (standard USB MIDI)
- **Maximum queued messages: ~32 MIDI events** (128 / 4)

### Configurable Buffer Sizes
```cpp
USBMIDI MIDI;
MIDI.setRXPacketSize(size);  // Can reduce from 64
MIDI.setTXPacketSize(size);  // Can reduce from 64
```

**WARNING:** Smaller packet sizes may slow things down and have not been thoroughly tested.

---

## Endpoint Limitations

### Hardware Constraints
- **1 bidirectional endpoint 0** (shared by all)
- **7 additional endpoints** available in each direction
- Same endpoint number in different directions must have matching parameters

### Endpoint Usage by Component
| Component | Endpoint Count | Direction |
|-----------|---------------|-----------|
| USB Serial | 2 | 2 TX, 1 RX |
| USB HID | 1 | 1 TX |
| USB Mass Storage | 1 | 1 TX, 1 RX |
| **USB MIDI** | **1** | **1 TX, 1 RX** |
| XBox360 Controller | 1 per controller | 1 TX, 1 RX |
| USB Audio | 1 | 1 TX or 1 RX |

**Rule of thumb:** Total endpoint contributions must be ≤ 7

---

## Why 6+ Simultaneous Key Presses Cause Indefinite Note Playback

### Scenario Analysis

1. **Normal Operation (1-5 keys)**
   - Key press → Note On sent → Buffer has space → USB transfers complete
   - Key release → Note Off sent → Buffer has space → Note terminates correctly

2. **Problem Scenario (6+ keys rapidly pressed)**
   - Multiple Note On messages flood the buffer
   - If host is slow to read (or buffer already partially full):
     - Buffer reaches 32-message capacity
     - **Next sendNoteOn() blocks waiting for buffer space**
     - Main loop hangs in USB transfer routine
     - **Key scan routine never executes**
     - Key release is never detected
     - Note Off is never sent
     - **Note plays indefinitely**

3. **Compounding Factors**
   - If using `midi.poll()` for incoming messages, this also requires buffer space
   - Active Sensing messages (sent every ~300ms by some hosts) consume buffer
   - Composite devices (MIDI + Serial) share the 320-byte pool

### Critical Code Pattern to Avoid
```cpp
// DANGEROUS: No buffer space checking
void handleKeyPress() {
    midi.sendNoteOn(channel, note, velocity);  // BLOCKS if buffer full!
}

void handleKeyRelease() {
    midi.sendNoteOff(channel, note, 0);  // Never reached if above blocks
}
```

### Recommended Mitigation
```cpp
// SAFER: Check connection/buffer status before sending
void handleKeyPress() {
    if (midi.isConnected()) {  // Or implement buffer space check
        midi.sendNoteOn(channel, note, velocity);
    }
}
```

---

## Known Bugs and Limitations

1. **No built-in buffer overflow protection** in USBComposite_stm32f1
2. **No flush functionality** to clear backed-up buffers
3. **No non-blocking send API** - all sends are blocking
4. **Connection detection requires workarounds** (timing-based as shown in issue)
5. **No host flow control feedback** - device doesn't know if host is reading

---

## STM32 USB Device Library Specifics

### USB FS (Full Speed) Limitations
- Maximum packet size: 64 bytes (FS endpoint limit)
- USB MIDI uses 4-byte Event Packets
- Maximum theoretical throughput: ~12 KB/s (64 bytes × 188 frames/s)
- Practical MIDI message rate: ~3000 messages/second max

### Interrupt Priority Considerations
- USB interrupt must have **higher priority** than key scanning
- If USB interrupt is blocked or delayed, buffer fills faster
- NVIC priority configuration critical for real-time response

---

## Recommendations for Fixing 6+ Key Issue

1. **Increase buffer monitoring**
   - Implement timeout on send operations
   - Add buffer space checking before sends

2. **Decouple key scanning from USB sending**
   - Use a queue for key events
   - Send from lower-priority task with timeout

3. **Implement note-off guarantee**
   - Track all sent Note Ons
   - Force Note Off on buffer timeout or disconnect

4. **Consider FreeRTOS integration**
   - Separate USB task with watchdog
   - Key scan task with dedicated queue
   - USB send with max wait time

5. **Reduce buffer usage**
   - Disable unused features (Active Sensing if not needed)
   - Use smaller packet sizes if throughput allows
