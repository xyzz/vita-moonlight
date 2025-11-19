#pragma once

#include <psp2/ctrl.h>
#include <stdbool.h>
#include <stdint.h>

#include <functional>

#include "ConfigManager.hpp"
#include "controller/GamepadState.hpp"
#include "controller/RearTouchInput.hpp"
#include "controller/TouchInput.hpp"

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

// Clase para manejar input en PS Vita
class ControllerInputManager
{
  public:
    ControllerInputManager();
    ~ControllerInputManager();

    // Procesar input y enviar eventos
    void handleInput();

    // Resetear estados de input (al desconectar)
    void dropInput();

    // Habilitar/deshabilitar procesamiento/envío de input (útil para overlays/menus)
    void setInputEnabled(bool enabled);

    void setTouchscreenMode(int mode) { touchscreenMode = mode; }

    // Validar y cambiar modo touch (con compatibilidad de gamepad)
    bool setTouchscreenModeWithValidation(int newMode);

    // Cambiar modo touch en tiempo de ejecución (como setGamepadType)
    // Valida compatibilidad, actualiza config y notifica cambio
    bool setTouchscreenModeRuntime(int newMode);

    // Enviar estado de gamepad
    void sendGamepadState(const GamepadState& state);

    // Callback para hotkey de pausa (START+L1+R1)
    void setPauseCallback(const std::function<void()>& cb);

    void applyRearTouchSettings(const RearTouchSettings& settings);
    void setRearTouchEnabled(bool enabled);

    // Cambiar tipo de gamepad sin reiniciar sesión
    void setGamepadType(GamepadType type);

  private:
    bool inputEnabled;
    bool inputDropped;

    // Estados anteriores para detectar cambios
    GamepadState lastGamepadState;
    VitaMouseState lastMouseState;
    SceCtrlData lastCtrlData;

    int touchscreenMode;

    TouchInputManager* touchManager;
    RearTouchInputManager* rearTouchManager;

    std::function<void()> pauseCallback;

    // Tipo de gamepad actual
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
