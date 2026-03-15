#include "navigation.h"
#include "constants.h"

static constexpr uint32_t INITIAL_DELAY_MS = 300;
static constexpr uint32_t REPEAT_DELAY_MS = 150;
static constexpr uint8_t CONSECUTIVE_READS_REQUIRED = 3;
static constexpr int16_t JOYSTICK_HYSTERESIS = 100;
static constexpr TickType_t MUTEX_TIMEOUT = pdMS_TO_TICKS(5);

// Joystick navigation state
struct JoystickState {
    int8_t xDirection = 0;
    uint8_t consecutiveX = 0;
    bool isUpActive = false;
    bool isDownActive = false;
    bool isPageActive = false;
    uint32_t nextUpRepeatMs = 0;
    uint32_t nextDownRepeatMs = 0;
    uint32_t nextPageRepeatMs = 0;
};

static JoystickState joy;

// Helper to calculate next repeat time
static inline uint32_t calcNextRepeat(uint32_t now, bool isFirstPress) {
    return now + (isFirstPress ? INITIAL_DELAY_MS : REPEAT_DELAY_MS);
}

static bool tryNavigatePage(int8_t direction, bool& outIsMenuMode) {
    MutexGuard lock(sysState.mutex, MUTEX_TIMEOUT);
    if (!lock) {
        outIsMenuMode = false;
        return false;
    }

    outIsMenuMode = (sysState.viewMode == static_cast<uint8_t>(ViewMode::Menu));
    if (!outIsMenuMode) return false;

    int newPage = static_cast<int>(sysState.activePage) + direction;
    if (newPage < 0 || newPage >= PAGE_COUNT) return false;

    sysState.activePage = static_cast<MenuPage>(newPage);
    return true;
}

static void cycleViewMode(int8_t direction) {
    MutexGuard lock(sysState.mutex, MUTEX_TIMEOUT);
    if (!lock) return;

    int newMode = static_cast<int>(sysState.viewMode) + direction;
    constexpr int count = static_cast<int>(ViewMode::Count);

    // Wrap around using modulo
    newMode = ((newMode % count) + count) % count;

    sysState.viewMode = static_cast<uint8_t>(newMode);
    sysState.menuMode = (newMode == static_cast<int>(ViewMode::Menu));
}

static void handleVerticalNav(int16_t joyY, uint32_t now) {
    // DOWN: cycle to next view mode (with repeat)
    if (joyY > JOY_DOWN_THRESHOLD) {
        if (now >= joy.nextDownRepeatMs) {
            bool isFirst = !joy.isDownActive;
            joy.isDownActive = true;
            cycleViewMode(+1);
            joy.nextDownRepeatMs = calcNextRepeat(now, isFirst);
        }
    } else if (joyY <= JOY_DOWN_THRESHOLD - JOYSTICK_HYSTERESIS) {
        joy.isDownActive = false;
        joy.nextDownRepeatMs = 0;
    }

    // UP: cycle to previous view mode (with repeat)
    if (joyY < JOY_UP_THRESHOLD) {
        if (now >= joy.nextUpRepeatMs) {
            bool isFirst = !joy.isUpActive;
            joy.isUpActive = true;
            cycleViewMode(-1);
            joy.nextUpRepeatMs = calcNextRepeat(now, isFirst);
        }
    } else if (joyY >= JOY_UP_THRESHOLD + JOYSTICK_HYSTERESIS) {
        joy.isUpActive = false;
        joy.nextUpRepeatMs = 0;
    }
}

static void handleHorizontalNav(int16_t joyX, uint32_t now) {
    int8_t newDir = 0;
    if (joyX < JOY_CENTER_X - JOY_THRESHOLD - JOYSTICK_HYSTERESIS) {
        newDir = -1;
    } else if (joyX > JOY_CENTER_X + JOY_THRESHOLD + JOYSTICK_HYSTERESIS) {
        newDir = +1;
    } else if (joyX > JOY_CENTER_X - JOY_THRESHOLD && joyX < JOY_CENTER_X + JOY_THRESHOLD) {
        // In dead zone - reset state
        joy.xDirection = 0;
        joy.consecutiveX = 0;
        joy.isPageActive = false;
        joy.nextPageRepeatMs = 0;
        return;
    } else {
        // In hysteresis zone - maintain current direction
        newDir = joy.xDirection;
    }

    if (newDir == 0) {
        joy.consecutiveX = 0;
        joy.isPageActive = false;
        joy.nextPageRepeatMs = 0;
        joy.xDirection = 0;
        return;
    }

    if (newDir == joy.xDirection) {
        if (joy.consecutiveX < UINT8_MAX) {
            joy.consecutiveX++;
        }
    } else {
        joy.consecutiveX = 1;
        joy.isPageActive = false;
        joy.nextPageRepeatMs = 0;
    }
    joy.xDirection = newDir;

    if (joy.consecutiveX >= CONSECUTIVE_READS_REQUIRED && now >= joy.nextPageRepeatMs) {
        bool isMenuMode;
        if (tryNavigatePage(joy.xDirection, isMenuMode)) {
            bool isFirst = !joy.isPageActive;
            joy.isPageActive = true;
            joy.nextPageRepeatMs = calcNextRepeat(now, isFirst);
        } else if (!isMenuMode) {
            // Not in menu mode, don't keep trying
            joy.nextPageRepeatMs = UINT32_MAX;
        }
    }
}

void navUpdate(int16_t joyX, int16_t joyY) {
    uint32_t now = millis();
    handleVerticalNav(joyY, now);
    handleHorizontalNav(joyX, now);
}
