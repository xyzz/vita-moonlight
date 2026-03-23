#include "controller/TouchInput.hpp"

#include <psp2/kernel/threadmgr.h>
#include <psp2/touch.h>

#include <cmath>
#include <cstring>

#include "ConfigManager.hpp"
#include "Limelight.h"
#include "debug.hpp"

TouchInputManager* g_touchInput = nullptr;

#define WIDTH 960
#define HEIGHT 544
#define SCROLL_THRESHOLD 12

TouchInputManager::TouchInputManager()
    : currentMode(TOUCHSCREEN_MODE_OFF)
{
    memset(&touchData, 0, sizeof(SceTouchData));
    memset(&lastTouchData, 0, sizeof(SceTouchData));
    memset(&ds4State, 0, sizeof(ds4State));
    memset(&absoluteState, 0, sizeof(absoluteState));
    memset(&tabletState, 0, sizeof(tabletState));

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

    ConfigManager config;
    config.load();
    VideoSettings settings      = config.getVideoSettings();
    trackpadPointerSpeed        = settings.trackpad_pointer_speed;
    trackpadDeadZone            = settings.trackpad_dead_zone;
    trackpadTapToClick          = true; // Always on on PS Vita
    trackpadTwoFingerRightClick = settings.trackpad_two_finger_right_click;
    trackpadTwoFingerScroll     = settings.trackpad_two_finger_scroll;
    trackpadInvertScroll        = settings.trackpad_invert_scroll;
    trackpadMultiTouch          = settings.trackpad_multi_touch;
    trackpadEdgeZone            = settings.trackpad_edge_zone;

    vita_debug_log("[TouchInput] Initialized with mode OFF");
}

TouchInputManager::~TouchInputManager()
{
}

bool TouchInputManager::isModeSupportedByGamepad(int touchscreenMode, int gamepadType)
{
    if (touchscreenMode == TOUCHSCREEN_MODE_DS4_TOUCHPAD)
    {
        return (gamepadType == 2);
    }
    return true;
}

bool TouchInputManager::setTouchMode(int newMode, int gamepadType)
{
    if (!isModeSupportedByGamepad(newMode, gamepadType))
    {
        vita_debug_log("[TouchInput][WARN] Modo %d no soportado con gamepad tipo %d", newMode, gamepadType);
        if (newMode == TOUCHSCREEN_MODE_DS4_TOUCHPAD && gamepadType == 1)
        {
            vita_debug_log("[TouchInput] DS4 Touchpad solo compatible con PlayStation");
        }
        return false;
    }

    if (currentMode != newMode)
    {
        dropTouch(currentMode);
        currentMode = newMode;
        vita_debug_log("[TouchInput] Modo cambiado a %d", newMode);
    }
    return true;
}

void TouchInputManager::handleTouch(int touchscreenMode)
{
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touchData, 1);

    if (touchscreenMode == TOUCHSCREEN_MODE_OFF)
    {
        return;
    }

    switch (touchscreenMode)
    {
        case TOUCHSCREEN_MODE_TRACKPAD:
            handleTrackpad();
            break;
        case TOUCHSCREEN_MODE_DS4_TOUCHPAD:
            handleDS4Touch();
            break;
        case TOUCHSCREEN_MODE_MOUSE_ABSOLUTE:
            handleMouseAbsolute();
            break;
        case TOUCHSCREEN_MODE_TABLET:
            handleTabletTouch();
            break;
        default:
            break;
    }

    lastTouchData = touchData;
}

void TouchInputManager::handleTrackpad()
{
    // State machine for trackpad (based on original vita.c):
    // - TAP (< 100ms): press and release the button
    // - SWIPE (> 100ms): just move the mouse WITHOUT pressing button

    // OPTIMIZED: Use member variables (cache) instead of reading from disk every frame
    // Las variables se actualizan solo desde applyTrackpadSettingsLive() → setTrackpadSettings()
    int tapMoveThreshold     = trackpadDeadZone;
    bool twoFingerRightClick = trackpadTwoFingerRightClick;
    bool twoFingerScroll     = trackpadTwoFingerScroll;
    bool invertScroll        = trackpadInvertScroll;

    static const int MOUSE_ACTION_DELAY_MS = 100; // 100ms to detect TAP vs SWIPE

    uint64_t now = sceKernelGetSystemTimeWide();

    if (touchData.reportNum == 0)
    {
        if (trackpadState == 1)
        { // If we were waiting for TAP/SWIPE and the last finger was raised
            uint64_t elapsedMs     = (now - trackpadStateStartTime) / 1000;
            SceTouchReport current = trackpadInitialTouch;
            int moveX              = 0;
            int moveY              = 0;
            int totalMovement      = 0;
            if (trackpadFingerCount > 0)
            {
                moveX         = abs(current.x - trackpadInitialTouch.x);
                moveY         = abs(current.y - trackpadInitialTouch.y);
                totalMovement = moveX + moveY;
            }
            if (elapsedMs <= MOUSE_ACTION_DELAY_MS && totalMovement <= tapMoveThreshold)
            {
                vita_debug_log("[TRACKPAD] TAP detected (release) with %d finger(s) (move=%d)", trackpadFingerCount, totalMovement);
                if (trackpadFingerCount == 1)
                {
                    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
                    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
                }
                else if (trackpadFingerCount == 2 && twoFingerRightClick)
                {
                    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT);
                    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
                }
            }
            else
            {
                vita_debug_log("[TRACKPAD] Invalid TAP (release) (time=%llu ms, move=%d)", elapsedMs, totalMovement);
            }
        }
        else if (trackpadState == 2)
        { // SCREEN_TAP - we were on TAP waiting for timeout to drop
            // Release the button that was pressed in ON_TOUCH
            if (trackpadFingerCount == 1)
            {
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
            }
            else if (trackpadFingerCount == 2 && twoFingerRightClick)
            {
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
            }
            vita_debug_log("[TRACKPAD] TAP released");
        }
        trackpadState       = 0; // Return to NO_TOUCH
        trackpadFingerCount = 0;
        return;
    }

    switch (trackpadState)
    {
        case 0: // NO_TOUCH - start of contact
            if (touchData.reportNum > 0)
            {
                trackpadState          = 1; // ON_TOUCH
                trackpadFingerCount    = touchData.reportNum;
                trackpadStateStartTime = now;
                trackpadInitialTouch   = touchData.report[0]; // Save initial position
                vita_debug_log("[TRACKPAD] Touch start with %d finger(s)", trackpadFingerCount);
            }
            break;

        case 1: // ON_TOUCH - waiting to determine if it is TAP or SWIPE
        {
            uint64_t elapsedMs = (now - trackpadStateStartTime) / 1000; // Convert to ms

            SceTouchReport current = touchData.report[0];
            int moveX              = abs(current.x - trackpadInitialTouch.x);
            int moveY              = abs(current.y - trackpadInitialTouch.y);
            int totalMovement      = moveX + moveY;

            if (touchData.reportNum < trackpadFingerCount)
            {
                if (elapsedMs <= MOUSE_ACTION_DELAY_MS && totalMovement <= tapMoveThreshold)
                {
                    vita_debug_log("[TRACKPAD] TAP detected with %d finger(s) (move=%d)", trackpadFingerCount, totalMovement);
                    if (trackpadFingerCount == 1)
                    {
                        LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
                    }
                    else if (trackpadFingerCount == 2 && twoFingerRightClick)
                    {
                        LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT);
                    }
                    trackpadState          = 2; // SCREEN_TAP
                    trackpadStateStartTime = now; // Start timeout to release
                }
                else
                {
                    vita_debug_log("[TRACKPAD] Invalid TAP (time=%llu ms, move=%d)", elapsedMs, totalMovement);
                    trackpadState = 0;
                }
            }
            else if (touchData.reportNum > trackpadFingerCount)
            {
                trackpadFingerCount = touchData.reportNum;
            }
            else if (elapsedMs > MOUSE_ACTION_DELAY_MS || totalMovement > tapMoveThreshold)
            {
                // The timeout has passed or it has moved too much -> it is a SWIPE
                trackpadState      = 3; // SWIPE_START
                trackpadSwipeStart = touchData.report[0];
                vita_debug_log("[TRACKPAD] SWIPE detected (time=%llu ms, move=%d)", elapsedMs, totalMovement);
            }
        }
        break;

        case 2: // SCREEN_TAP - waiting to release the button after the TAP
        {
            uint64_t elapsedMs = (now - trackpadStateStartTime) / 1000;
            if (elapsedMs > MOUSE_ACTION_DELAY_MS)
            {
                if (trackpadFingerCount == 1)
                {
                    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
                }
                else if (trackpadFingerCount == 2 && twoFingerRightClick)
                {
                    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
                }
                trackpadState = 0; // Return to NO_TOUCH
                vita_debug_log("[TRACKPAD] TAP button released after timeout");
            }
        }
        break;

        case 3: // SWIPE_START - prepare to drag (without pressing button)
        {
            trackpadSwipeStart = touchData.report[0];
            trackpadState      = 4; // ON_SWIPE
            vita_debug_log("[TRACKPAD] SWIPE started (no button press)");
        }
        break;

        case 4: // ON_SWIPE - drag WITHOUT pressing button
        {
            if (trackpadFingerCount == 1)
            {
                SceTouchReport report = touchData.report[0];
                float baseDeltaX      = (report.x - trackpadSwipeStart.x) / 2.0f;
                float baseDeltaY      = (report.y - trackpadSwipeStart.y) / 2.0f;
                float speedFactor     = trackpadPointerSpeed / 100.0f;
                int deltaX            = (int)std::lround(baseDeltaX * speedFactor);
                int deltaY            = (int)std::lround(baseDeltaY * speedFactor);

                if (deltaX != 0 || deltaY != 0)
                {
                    LiSendMouseMoveEvent(deltaX, deltaY);
                }
                trackpadSwipeStart = report;
            }
            else if (trackpadFingerCount == 2 && touchData.reportNum >= 2 && twoFingerScroll)
            {
                int avgY     = (touchData.report[0].y + touchData.report[1].y) / 2;
                int lastAvgY = trackpadSwipeStart.y;
                int deltaY   = (avgY - lastAvgY) / 2;

                if (deltaY != 0)
                {
                    int scrollDelta = invertScroll ? deltaY : -deltaY;
                    LiSendScrollEvent(scrollDelta);
                    trackpadSwipeStart.y = avgY;
                }
            }
        }
        break;
    }
}

void TouchInputManager::resetTrackpadState()
{
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_MIDDLE);

    trackpadModeState.lastMouseDown = false;
    trackpadModeState.lastAbsX      = -1;
    trackpadModeState.lastAbsY      = -1;

    vita_debug_log("[TRACKPAD] State reset - all mouse buttons released");
}

void TouchInputManager::handleDS4Touch()
{
    for (int j = 0; j < lastTouchData.reportNum; j++)
    {
        SceTouchReport lastReport = lastTouchData.report[j];
        bool stillPressed         = false;
        for (int i = 0; i < touchData.reportNum; i++)
        {
            if (touchData.report[i].id == lastReport.id)
            {
                stillPressed = true;
                break;
            }
        }
        if (!stillPressed)
        {
            LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_UP, lastReport.id, 0.0f, 0.0f, 0.0f);
            vita_debug_log("[DS4_TOUCHPAD] UP finger=%d", lastReport.id);
        }
    }

    for (int i = 0; i < touchData.reportNum; i++)
    {
        SceTouchReport report = touchData.report[i];
        uint8_t eventType     = LI_TOUCH_EVENT_MOVE;

        bool wasPressed = false;
        for (int j = 0; j < lastTouchData.reportNum; j++)
        {
            if (lastTouchData.report[j].id == report.id)
            {
                wasPressed = true;
                break;
            }
        }
        if (!wasPressed)
        {
            eventType = LI_TOUCH_EVENT_DOWN;
            vita_debug_log("[DS4_TOUCHPAD] DOWN finger=%d", report.id);
        }

        float x = (float)report.x / 960.0f;
        float y = (float)report.y / 544.0f;
        LiSendControllerTouchEvent(0, eventType, report.id, x, y, 1.0f);
    }
}

void TouchInputManager::resetDS4State()
{
    memset(&ds4State, 0, sizeof(ds4State));
    for (int i = 0; i < 10; i++)
    {
        LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_UP, i, 0.0f, 0.0f, 0.0f);
    }
}

void TouchInputManager::handleMouseAbsolute()
{
    if (touchData.reportNum == 0)
    {
        if (absoluteState.fingerCount > 0)
        {
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
            absoluteState.fingerCount     = 0;
            absoluteState.twoFingerScroll = false;
        }
        return;
    }

    if (touchData.reportNum == 1)
    {
        SceTouchReport report = touchData.report[0];

        int screenX = (report.x * WIDTH) / 1920;
        int screenY = (report.y * HEIGHT) / 1088;
        LiSendMousePositionEvent(screenX, screenY, WIDTH, HEIGHT);

        if (absoluteState.fingerCount != 1)
        {
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
            absoluteState.fingerCount     = 1;
            absoluteState.twoFingerScroll = false;
            absoluteState.rightClickSent  = false;
        }
    }
    else if (touchData.reportNum == 2)
    {
        int avgY     = (touchData.report[0].y + touchData.report[1].y) / 2;
        int lastAvgY = (lastTouchData.reportNum >= 2) ? (lastTouchData.report[0].y + lastTouchData.report[1].y) / 2 : avgY;

        int deltaY = avgY - lastAvgY;

        if (absoluteState.fingerCount != 2)
        {
            absoluteState.fingerCount     = 2;
            absoluteState.twoFingerStartY = avgY;
            absoluteState.twoFingerLastY  = avgY;
            absoluteState.twoFingerScroll = false;
            absoluteState.rightClickSent  = false;
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
        }
        else
        {
            if (abs(avgY - absoluteState.twoFingerStartY) > SCROLL_THRESHOLD)
            {
                absoluteState.twoFingerScroll = true;
            }

            if (absoluteState.twoFingerScroll && deltaY != 0)
            {
                LiSendScrollEvent(-deltaY / 2);
                absoluteState.twoFingerLastY = avgY;
            }
        }
    }
    else if (touchData.reportNum >= 3)
    {
        if (absoluteState.fingerCount < 3)
        {
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_MIDDLE);
            absoluteState.fingerCount = 3;
        }
    }
}

void TouchInputManager::resetAbsoluteState()
{
    memset(&absoluteState, 0, sizeof(absoluteState));
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_MIDDLE);
}

void TouchInputManager::handleTabletTouch()
{
    for (int j = 0; j < lastTouchData.reportNum; j++)
    {
        SceTouchReport lastReport = lastTouchData.report[j];
        bool stillPressed         = false;
        for (int i = 0; i < touchData.reportNum; i++)
        {
            if (touchData.report[i].id == lastReport.id)
            {
                stillPressed = true;
                break;
            }
        }
        if (!stillPressed)
        {
            float x = ((float)lastReport.x * WIDTH) / 1920.0f / WIDTH; // = lastReport.x / 1920.0f
            float y = ((float)lastReport.y * HEIGHT) / 1088.0f / HEIGHT; // = lastReport.y / 1088.0f

            LiSendTouchEvent(LI_TOUCH_EVENT_UP, lastReport.id, x, y, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
            vita_debug_log("[TABLET] UP finger=%d x=%.3f y=%.3f", lastReport.id, x, y);
        }
    }

    for (int i = 0; i < touchData.reportNum; i++)
    {
        SceTouchReport report = touchData.report[i];
        uint8_t eventType     = LI_TOUCH_EVENT_MOVE;

        bool wasPressed = false;
        for (int j = 0; j < lastTouchData.reportNum; j++)
        {
            if (lastTouchData.report[j].id == report.id)
            {
                wasPressed = true;
                break;
            }
        }
        if (!wasPressed)
        {
            eventType = LI_TOUCH_EVENT_DOWN;
            vita_debug_log("[TABLET] DOWN finger=%d", report.id);
        }

        float x = (float)report.x / 1920.0f;
        float y = (float)report.y / 1088.0f;
        LiSendTouchEvent(eventType, report.id, x, y, 1.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);

        if (eventType == LI_TOUCH_EVENT_DOWN)
        {
            vita_debug_log("[TABLET] DOWN finger=%d x=%.3f y=%.3f", report.id, x, y);
        }
    }
}

void TouchInputManager::resetTabletState()
{
    memset(&tabletState, 0, sizeof(tabletState));
    for (int i = 0; i < 10; i++)
    {
        LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL, i, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
    }
}

void TouchInputManager::dropTouch(int touchscreenMode)
{
    switch (touchscreenMode)
    {
        case TOUCHSCREEN_MODE_TRACKPAD:
            resetTrackpadState();
            break;
        case TOUCHSCREEN_MODE_DS4_TOUCHPAD:
            resetDS4State();
            break;
        case TOUCHSCREEN_MODE_MOUSE_ABSOLUTE:
            resetAbsoluteState();
            break;
        case TOUCHSCREEN_MODE_TABLET:
            resetTabletState();
            break;
        default:
            break;
    }
}

void TouchInputManager::setTrackpadSettings(int pointerSpeed, int deadZone, bool tapToClick,
    bool twoFingerRightClick, bool twoFingerScroll,
    bool invertScroll, bool multiTouch, int edgeZone)
{
    if (pointerSpeed < 0)
        pointerSpeed = 0;
    if (pointerSpeed > 200)
        pointerSpeed = 200;
    trackpadPointerSpeed        = pointerSpeed;
    trackpadDeadZone            = deadZone;
    trackpadTapToClick          = true; // Always on on PS Vita
    trackpadTwoFingerRightClick = twoFingerRightClick;
    trackpadTwoFingerScroll     = twoFingerScroll;
    trackpadInvertScroll        = invertScroll;
    trackpadMultiTouch          = multiTouch;
    trackpadEdgeZone            = edgeZone;

    vita_debug_log("[TRACKPAD] Settings updated: speed=%d, deadzone=%d, scroll=%d",
        pointerSpeed, deadZone, twoFingerScroll);
}

int TouchInputManager::getPointerSpeed() const
{
    return trackpadPointerSpeed;
}

int TouchInputManager::getDeadZone() const
{
    return trackpadDeadZone;
}

bool TouchInputManager::isTapToClickEnabled() const
{
    return true; // Always on on PS Vita
}

bool TouchInputManager::isTwoFingerRightClickEnabled() const
{
    return trackpadTwoFingerRightClick;
}

bool TouchInputManager::isTwoFingerScrollEnabled() const
{
    return trackpadTwoFingerScroll;
}

bool TouchInputManager::isInvertScrollEnabled() const
{
    return trackpadInvertScroll;
}

bool TouchInputManager::isMultiTouchEnabled() const
{
    return trackpadMultiTouch;
}

int TouchInputManager::getEdgeZone() const
{
    return trackpadEdgeZone;
}
