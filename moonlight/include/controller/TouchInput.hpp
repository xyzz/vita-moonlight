#pragma once

#include <psp2/touch.h>
#include <stdint.h>

#include "controller/touchscreen_modes.hpp"

class TouchInputManager
{
  public:
    TouchInputManager();
    ~TouchInputManager();

    void handleTouch(int touchscreenMode);

    void dropTouch(int touchscreenMode);

    bool isModeSupportedByGamepad(int touchscreenMode, int gamepadType);

    bool setTouchMode(int newMode, int gamepadType);

    void setTrackpadSettings(int pointerSpeed, int deadZone, bool tapToClick,
        bool twoFingerRightClick, bool twoFingerScroll,
        bool invertScroll, bool multiTouch, int edgeZone);

    int getPointerSpeed() const;
    int getDeadZone() const;
    bool isTapToClickEnabled() const;
    bool isTwoFingerRightClickEnabled() const;
    bool isTwoFingerScrollEnabled() const;
    bool isInvertScrollEnabled() const;
    bool isMultiTouchEnabled() const;
    int getEdgeZone() const;

  private:
    SceTouchData touchData;
    SceTouchData lastTouchData;
    int currentMode;

    int trackpadPointerSpeed         = 100; // 0-200
    int trackpadDeadZone             = 50; // 0-200px
    bool trackpadTapToClick          = true;
    bool trackpadTwoFingerRightClick = true;
    bool trackpadTwoFingerScroll     = true;
    bool trackpadInvertScroll        = false;
    bool trackpadMultiTouch          = true;
    int trackpadEdgeZone             = 15; // 0-50%

    int trackpadState                   = 0;
    int trackpadFingerCount             = 0;
    uint64_t trackpadStateStartTime     = 0;
    SceTouchReport trackpadInitialTouch = { 0 };
    SceTouchReport trackpadSwipeStart   = { 0 };

    struct
    {
        bool lastMouseDown;
        int lastAbsX, lastAbsY;
    } trackpadModeState;

    struct
    {
        uint8_t prevFingerActive[10];
        int prevX[10], prevY[10];
    } ds4State;

    struct
    {
        int frontState;
        short fingerCount;
        uint32_t twoFingerStartTime;
        int twoFingerStartY;
        int twoFingerLastY;
        bool twoFingerScroll;
        bool rightClickSent;
    } absoluteState;

    struct
    {
        uint8_t prevFingerActive[10];
    } tabletState;

    void handleTrackpad();
    void handleDS4Touch();
    void handleMouseAbsolute(); // Mode 3: Absolute mouse
    void handleTabletTouch();

    void resetTrackpadState();
    void resetDS4State();
    void resetAbsoluteState();
    void resetTabletState();
};

extern TouchInputManager* g_touchInput;
