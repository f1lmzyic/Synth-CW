# Build Fixes Applied

## Issue 1: Conflicting voiceEnvState declarations

**Error:**
```
include/envelopes.h:22:17: error: conflicting declaration 'EnvState voiceEnvState [8]'
include/voice_engine.h:21:16: note: previous declaration as 'uint8_t voiceEnvState [8]'
```

**Root Cause:**
Both `voice_engine.h` and `envelopes.h` were declaring the same voice state arrays:
- `voiceKey[POLYPHONY]`
- `voiceActive[POLYPHONY]`
- `voiceRetrigger[POLYPHONY]`
- `voiceEnvState[POLYPHONY]`
- `voiceEnvValue[POLYPHONY]`

**Fix:**
1. **include/envelopes.h** - Removed duplicate extern declarations for voice state arrays
   - Voice state is owned by voice_engine module
   - Added comment explaining that voice state is declared in voice_engine.h

2. **src/envelopes.cpp** - Removed duplicate definitions
   - Removed: `voiceKey`, `voiceActive`, `voiceRetrigger`, `voiceEnvState`, `voiceEnvValue`
   - Kept only: `modEnvState`, `modEnvValue` (owned by envelopes module)
   - Added: `#include "voice_engine.h"` to access voice state arrays

## Issue 2: Missing OUTR_PIN definition

**Error:**
```
src/dsp_core.cpp:120:17: error: 'OUTR_PIN' was not declared in this scope
```

**Root Cause:**
`dsp_core.cpp` uses `OUTR_PIN` but didn't include `constants.h`

**Fix:**
- **src/dsp_core.cpp** - Added `#include "constants.h"`

## Files Modified

1. `include/envelopes.h` - Removed duplicate voice state declarations
2. `src/envelopes.cpp` - Removed duplicate definitions, added voice_engine.h include
3. `src/dsp_core.cpp` - Added constants.h include

## Verification

Voice state arrays now declared ONLY in:
- **Declaration**: `include/voice_engine.h` (extern)
- **Definition**: `src/voice_engine.cpp` (actual storage)

All other modules access voice state via `#include "voice_engine.h"`

## Build Command

```bash
platformio run
```

Expected result: Clean build with no errors
