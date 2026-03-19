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

static AxisState axisX, axisY;

// Returns -1, 0, or +1 relative to joystick center.
// Hysteresis: requires crossing JOY_DEADBAND to activate, but only releases
// when the value returns within JOY_DEADBAND/2 of center.
static int8_t joyDir(int16_t val, int16_t center, int8_t prevDir) {
  if (val < center - JOY_DEADBAND) return -1;
  if (val > center + JOY_DEADBAND) return +1;
  // stay active if still past the release threshold
  constexpr int16_t EXIT = JOY_DEADBAND / 2;
  if (prevDir == -1 && val < center - EXIT) return -1;
  if (prevDir == +1 && val > center + EXIT) return +1;
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
      if (!sysState.menuMode) {
        sysState.menuMode = true;
        sysState.viewMode = static_cast<uint8_t>(ViewMode::Menu);
      }
      int newPage = static_cast<int>(sysState.activePage) + dir;
      if (newPage < 0)           newPage = 0;
      if (newPage >= PAGE_COUNT) newPage = PAGE_COUNT - 1;
      sysState.activePage = static_cast<MenuPage>(newPage);
    }
    axisX.nextMs = now + (axisX.fired ? REPEAT_DELAY_MS : INITIAL_DELAY_MS);
    axisX.fired  = true;
  }
}

// Vertical: exit menu → performance, or cycle performance views
static void handleY(int8_t dir, uint32_t now) {
  if (dir != axisY.dir) {
    axisY.dir   = dir;
    axisY.fired = false;
  }
  if (dir == 0) return;

  if (!axisY.fired || now >= axisY.nextMs) {
    MutexGuard lock(sysState.mutex, MUTEX_TIMEOUT);
    if (lock) {
      if (sysState.menuMode) {
        sysState.menuMode = false;
        sysState.viewMode = static_cast<uint8_t>(ViewMode::Performance);
      } else {
        constexpr int nonMenuViews = static_cast<int>(ViewMode::Menu); // 3
        int newMode = (static_cast<int>(sysState.viewMode) + dir + nonMenuViews) % nonMenuViews;
        sysState.viewMode = static_cast<uint8_t>(newMode);
      }
    }
    axisY.nextMs = now + (axisY.fired ? REPEAT_DELAY_MS : INITIAL_DELAY_MS);
    axisY.fired  = true;
  }
}

void navUpdate(int16_t joyX, int16_t joyY, bool pitchBendActive) {
  uint32_t now = millis();
  
  // When pitch bend is active, disable ALL navigation (X and Y axes)
  if (!pitchBendActive) {
    handleX(joyDir(joyX, JOY_CENTER_X, axisX.dir), now);
    handleY(joyDir(joyY, JOY_CENTER_Y, axisY.dir), now);
  } else {
    // Reset both axes so navigation doesn't fire stale events
    // the moment pitch bend is toggled off while joystick is still deflected
    axisX.dir   = 0;
    axisX.fired = false;
    axisY.dir   = 0;
    axisY.fired = false;
  }
}
