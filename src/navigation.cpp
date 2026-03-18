#include "navigation.h"
#include "constants.h"

static constexpr uint32_t INITIAL_DELAY_MS = 300;
static constexpr uint32_t REPEAT_DELAY_MS  = 250;
static constexpr int16_t  JOY_DEADBAND     = 150;
static constexpr TickType_t MUTEX_TIMEOUT  = pdMS_TO_TICKS(5);

struct AxisState {
  int8_t  dir    = 0;
  bool    fired  = false;
  uint32_t nextMs = 0;
};

static AxisState axisX;

// Returns -1, 0, or +1 relative to joystick center
static int8_t joyDir(int16_t val, int16_t center) {
  if (val < center - JOY_DEADBAND) return -1;
  if (val > center + JOY_DEADBAND) return +1;
  return 0;
}

// Horizontal: auto-enter menu and navigate pages left/right
static void handleX(int8_t dir, uint32_t now) {
  if (dir != axisX.dir) {
    axisX.dir   = dir;
    axisX.fired = false;
  }
  if (dir == 0) return;

  if (!axisX.fired || now >= axisX.nextMs) {
    MutexGuard lock(sysState.mutex, MUTEX_TIMEOUT);
    if (lock) {
      int idx = static_cast<int>(sysState.activePage) + dir;
      idx = (idx + PAGE_COUNT) % PAGE_COUNT;
      sysState.activePage = static_cast<MenuPage>(idx);
    }
    axisX.nextMs = now + (axisX.fired ? REPEAT_DELAY_MS : INITIAL_DELAY_MS);
    axisX.fired  = true;
  }
}

void navUpdate(int16_t joyY, bool pitchBendActive) {
  uint32_t now = millis();

  // When pitch bend is active, disable navigation
  if (!pitchBendActive) {
    handleX(joyDir(joyY, JOY_CENTER_Y), now);
  } else {
    // Reset axis so navigation doesn't fire stale events
    axisX.dir   = 0;
    axisX.fired = false;
  }
}
