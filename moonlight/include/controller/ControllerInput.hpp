#pragma once

#include <psp2/ctrl.h>
#include <stdbool.h>
#include <stdint.h>

#include <functional>

#include "ConfigManager.hpp"
#include "controller/GamepadState.hpp"
#include "controller/RearTouchInput.hpp"
#include "controller/TouchInput.hpp"
#include "controller/keyboard/IKeyboard.hpp"
#include "debug.hpp"

struct VitaMouseState
{
    uint16_t buttonFlags;
    unsigned char leftTrigger;
    unsigned char rightTrigger;
    short leftStickX;
    short leftStickY;
    short rightStickX;
    short rightStickY;
};

class ControllerInputManager
{
  public:
    ControllerInputManager();
    ~ControllerInputManager();

    void handleInput();

    void dropInput();

    void setInputEnabled(bool enabled);

    void setTouchscreenMode(int mode) { touchscreenMode = mode; }

    bool setTouchscreenModeWithValidation(int newMode);

    bool setTouchscreenModeRuntime(int newMode);

    void sendGamepadState(const GamepadState& state);

    void setPauseCallback(const std::function<void()>& cb);
    void setKeyboardShortcutCallback(const std::function<void()>& cb);

    void applyRearTouchSettings(const RearTouchSettings& settings);
    void setRearTouchEnabled(bool enabled);

    void setGamepadType(GamepadType type);

    void setActiveKeyboard(IKeyboard* kb);
    IKeyboard* getActiveKeyboard() const { return activeKeyboard; }

  private:
    bool inputEnabled;
    bool inputDropped;
    bool touchSuppressed          = false;
    uint64_t touchSuppressUntilUs = 0;
    bool lastKeyboardOpen         = false;

    GamepadState lastGamepadState;
    VitaMouseState lastMouseState;
    SceCtrlData lastCtrlData;

    int touchscreenMode;

    TouchInputManager* touchManager;
    RearTouchInputManager* rearTouchManager;

    std::function<void()> pauseCallback;

    IKeyboard* activeKeyboard   = nullptr;
    bool activeKeyboardSeenOpen = false;

    GamepadType currentGamepadType = GAMEPAD_TYPE_XBOX;

    struct ButtonMapping
    {
        uint32_t btnDpadUp;
        uint32_t btnDpadDown;
        uint32_t btnDpadLeft;
        uint32_t btnDpadRight;
        uint32_t btnSouth;
        uint32_t btnEast;
        uint32_t btnNorth;
        uint32_t btnWest;
        uint32_t btnSelect;
        uint32_t btnStart;
        uint32_t btnL1;
        uint32_t btnR1;
        uint32_t btnL2;
        uint32_t btnR2;
        uint32_t btnL3;
        uint32_t btnR3;
        uint32_t absLX;
        uint32_t absLY;
        uint32_t absRX;
        uint32_t absRY;
    };

    ButtonMapping mapping;
    bool isPstvModel;

    void initMapping();
    GamepadState buildGamepadState(const SceCtrlData& ctrlData) const;
    bool isPressed(uint32_t binding, const SceCtrlData& pad) const;
    unsigned char readTrigger(uint32_t binding, const SceCtrlData& pad) const;
    short readAxis(uint32_t binding, const SceCtrlData& pad) const;
    short applyDeadzone(short value) const;
};

extern ControllerInputManager* g_controllerInput;
