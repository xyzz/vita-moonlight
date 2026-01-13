#include "controller/ControllerInput.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <thread>

#include "ConfigManager.hpp"
#include "Limelight.h"
#include "controller/input_types.hpp"
#include "controller/shortcuts.hpp"
#include "debug.hpp"

namespace
{

using controller::ANALOG_LEFT_TRIGGER;
using controller::ANALOG_RIGHT_TRIGGER;
using controller::INPUT_TYPE_ANALOG;
using controller::INPUT_TYPE_GAMEPAD;
using controller::INPUT_TYPE_MASK;
using controller::INPUT_VALUE_MASK;

enum AnalogBinding : uint32_t
{
    ANALOG_LEFT_X = 0,
    ANALOG_LEFT_Y,
    ANALOG_RIGHT_X,
    ANALOG_RIGHT_Y,
    ANALOG_LEFT_TRIGGER_BIND  = ANALOG_LEFT_TRIGGER,
    ANALOG_RIGHT_TRIGGER_BIND = ANALOG_RIGHT_TRIGGER,
};

inline uint32_t makeGamepadBinding(uint32_t buttonMask)
{
    return buttonMask ? (buttonMask | INPUT_TYPE_GAMEPAD) : 0;
}

inline uint32_t makeAnalogBinding(AnalogBinding binding)
{
    return static_cast<uint32_t>(binding) | INPUT_TYPE_ANALOG;
}

}

// Instancia global
ControllerInputManager* g_controllerInput = nullptr;

ControllerInputManager::ControllerInputManager()
    : inputEnabled(true)
    , inputDropped(false)
    , touchscreenMode(0)
    , touchManager(nullptr)
    , rearTouchManager(nullptr)
    , pauseCallback(nullptr)
    , activeKeyboard(nullptr)
{

    memset(&lastGamepadState, 0, sizeof(GamepadState));
    memset(&lastMouseState, 0, sizeof(VitaMouseState));
    memset(&lastCtrlData, 0, sizeof(SceCtrlData));
    memset(&mapping, 0, sizeof(mapping));

    // Inicializar controles
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    touchManager     = new TouchInputManager();
    g_touchInput     = touchManager;
    rearTouchManager = new RearTouchInputManager();

    ConfigManager config;
    config.load();
    VideoSettings initialSettings = config.getVideoSettings();
    rearTouchManager->updateSettings(initialSettings.rear_touch);

    // Cargar tipo de gamepad desde config
    currentGamepadType = initialSettings.gamepad_type;

    isPstvModel = (sceKernelGetModel() == SCE_KERNEL_MODEL_VITATV);
    initMapping();

    vita_debug_log("[ControllerInput] Initialized (gamepad_type=%d)", (int)currentGamepadType);
}

ControllerInputManager::~ControllerInputManager()
{
    if (touchManager)
    {
        if (g_touchInput == touchManager)
        {
            g_touchInput = nullptr;
        }
        delete touchManager;
        touchManager = nullptr;
    }
    if (rearTouchManager)
    {
        delete rearTouchManager;
        rearTouchManager = nullptr;
    }
}

void ControllerInputManager::handleInput()
{
    using namespace std::chrono;
    if (!inputEnabled)
        return;

    inputDropped = false;
    auto t_start = high_resolution_clock::now();

    // Leer controles
    SceCtrlData ctrlData;
    sceCtrlPeekBufferPositive(0, &ctrlData, 1);

    if (!isPstvModel)
    {
        // En PS Vita portátil, los botones físicos L/R aparecen como LTRIGGER/RTRIGGER.
        // Normalizamos para que también activen los flags de L1/R1 requeridos por los shortcuts.
        if (ctrlData.buttons & SCE_CTRL_LTRIGGER)
        {
            ctrlData.buttons |= SCE_CTRL_L1;
        }
        if (ctrlData.buttons & SCE_CTRL_RTRIGGER)
        {
            ctrlData.buttons |= SCE_CTRL_R1;
        }
    }

    // Debug: mostrar botones presionados y valores de triggers
    static int debug_counter = 0;
    if (debug_counter++ % 60 == 0)
    { // Cada segundo aproximadamente
        vita_debug_log("[ControllerInput] Botones: 0x%08X (L1:%d R1:%d L2:%d R2:%d START:%d) LT:%d RT:%d",
            ctrlData.buttons,
            (ctrlData.buttons & SCE_CTRL_L1) ? 1 : 0,
            (ctrlData.buttons & SCE_CTRL_R1) ? 1 : 0,
            (ctrlData.buttons & SCE_CTRL_L2) ? 1 : 0,
            (ctrlData.buttons & SCE_CTRL_R2) ? 1 : 0,
            (ctrlData.buttons & SCE_CTRL_START) ? 1 : 0,
            ctrlData.lt,
            ctrlData.rt);
    }

    // Procesar shortcuts físicos
    if (process_physical_shortcuts(&ctrlData, &lastCtrlData))
    {
        // Shortcut ejecutado, no procesar input normal
        vita_debug_log("[ControllerInput] Shortcut ejecutado, retornando temprano");
        lastCtrlData = ctrlData;
        return;
    }

    GamepadState gamepadState = buildGamepadState(ctrlData);
    if (rearTouchManager)
    {
        rearTouchManager->process(gamepadState, isPstvModel);
    }

    // Enviar si cambió
    if (memcmp(&gamepadState, &lastGamepadState, sizeof(GamepadState)) != 0)
    {
        sendGamepadState(gamepadState);
        lastGamepadState = gamepadState;
    }

    // Actualizar estado anterior
    lastCtrlData = ctrlData;

    // Manejar táctil basado en modo
    touchManager->handleTouch(touchscreenMode);

    // Keyboard Polling
    if (activeKeyboard)
    {
        static KeyboardState oldKeyboardState;
        KeyboardState keyboardState = activeKeyboard->getKeyboardState();

        for (int i = 0; i < 256; ++i)
        {
            if (keyboardState.keys[i] != oldKeyboardState.keys[i])
            {
                oldKeyboardState.keys[i] = keyboardState.keys[i];
                LiSendKeyboardEvent(
                    (short)i,
                    keyboardState.keys[i] ? KEY_ACTION_DOWN : KEY_ACTION_UP,
                    0);
                vita_debug_log("[ControllerInput] Keyboard VK 0x%02X -> %s",
                    i, keyboardState.keys[i] ? "DOWN" : "UP");
            }
        }
    }

    // Medir tiempo total de handleInput y loggear periódicamente para detectar bloqueos
    auto t_end                     = high_resolution_clock::now();
    auto dur_us                    = duration_cast<microseconds>(t_end - t_start).count();
    uint64_t now_ms                = duration_cast<milliseconds>(t_end.time_since_epoch()).count();
    static uint64_t lastInputLogMs = 0;
    if (now_ms - lastInputLogMs > 500)
    {
        lastInputLogMs = now_ms;
        vita_debug_log("[ControllerInput][PERF] handleInput time=%lld us inputEnabled=%d inputDropped=%d", (long long)dur_us, inputEnabled ? 1 : 0, inputDropped ? 1 : 0);
    }
}

// Enviar estado de gamepad
void ControllerInputManager::sendGamepadState(const GamepadState& state)
{
    if (LiSendMultiControllerEvent(0, 1, state.buttonFlags, state.leftTrigger, state.rightTrigger,
            state.leftStickX, state.leftStickY, state.rightStickX, state.rightStickY)
        != 0)
    {
        vita_debug_log("[ControllerInput] Failed to send gamepad state");
    }
}

void ControllerInputManager::dropInput()
{
    if (inputDropped)
        return;

    GamepadState zeroState = { 0 };
    sendGamepadState(zeroState);

    // Reset touch basado en modo
    touchManager->dropTouch(touchscreenMode);
    if (rearTouchManager)
    {
        rearTouchManager->dropState();
    }

    inputDropped = true;
    vita_debug_log("[ControllerInput] Input dropped");
}

// Callback para hotkey de pausa (START+L1+R1)
void ControllerInputManager::setPauseCallback(const std::function<void()>& cb)
{
    pauseCallback = cb;
    vita_debug_log("[ControllerInput] setPauseCallback llamado, configurando callback");
    set_pause_callback(cb);
}

// Public setter para habilitar/deshabilitar el procesamiento de input.
// Cuando se deshabilita, se hace un dropInput() inmediato para enviar estado cero
// al host y evitar que queden botones presionados en la transmisión.
void ControllerInputManager::setInputEnabled(bool enabled)
{
    this->inputEnabled = enabled;
    if (!enabled)
    {
        // Enviar estado cero INMEDIATO de forma asíncrona para no bloquear el hilo UI.
        // dropInput() puede invocar LiSendMultiControllerEvent que a veces
        // puede bloquear si la conexión está en un mal estado; llamarlo en
        // un hilo separado evita que la UI sufra hitches al abrir overlays.
        std::thread([this]()
            { this->dropInput(); })
            .detach();
    }
    vita_debug_log("[ControllerInput] setInputEnabled -> %d", enabled ? 1 : 0);
}

void ControllerInputManager::initMapping()
{
    mapping.btnDpadUp    = makeGamepadBinding(SCE_CTRL_UP);
    mapping.btnDpadDown  = makeGamepadBinding(SCE_CTRL_DOWN);
    mapping.btnDpadLeft  = makeGamepadBinding(SCE_CTRL_LEFT);
    mapping.btnDpadRight = makeGamepadBinding(SCE_CTRL_RIGHT);

    mapping.btnSouth = makeGamepadBinding(SCE_CTRL_CROSS);
    mapping.btnEast  = makeGamepadBinding(SCE_CTRL_CIRCLE);
    mapping.btnNorth = makeGamepadBinding(SCE_CTRL_TRIANGLE);
    mapping.btnWest  = makeGamepadBinding(SCE_CTRL_SQUARE);

    mapping.btnSelect = makeGamepadBinding(SCE_CTRL_SELECT);
    mapping.btnStart  = makeGamepadBinding(SCE_CTRL_START);

    mapping.btnL1 = makeGamepadBinding(SCE_CTRL_L1);
    mapping.btnR1 = makeGamepadBinding(SCE_CTRL_R1);
    mapping.btnL3 = makeGamepadBinding(SCE_CTRL_L3);
    mapping.btnR3 = makeGamepadBinding(SCE_CTRL_R3);

    mapping.absLX = makeAnalogBinding(ANALOG_LEFT_X);
    mapping.absLY = makeAnalogBinding(ANALOG_LEFT_Y);
    mapping.absRX = makeAnalogBinding(ANALOG_RIGHT_X);
    mapping.absRY = makeAnalogBinding(ANALOG_RIGHT_Y);

    if (isPstvModel)
    {
        mapping.btnL2 = makeAnalogBinding(ANALOG_LEFT_TRIGGER_BIND);
        mapping.btnR2 = makeAnalogBinding(ANALOG_RIGHT_TRIGGER_BIND);
    }
    else
    {
        // En PS Vita portátil no hay gatillos físicos analógicos
        mapping.btnL2 = 0;
        mapping.btnR2 = 0;
    }
}

GamepadState ControllerInputManager::buildGamepadState(const SceCtrlData& ctrlData) const
{
    GamepadState state {};

    // Mapeo de botones depende del tipo de gamepad
    // Ambos tipos usan el mismo layout de botones Vita (X/O/△/□ = sur/este/norte/oeste)
    // La diferencia está en cómo el host interpreta estos botones según el tipo reportado
    // Xbox: A/B/X/Y siguen el estándar Xbox (Cross=A, Circle=B, Square=X, Triangle=Y)

    if (isPressed(mapping.btnDpadUp, ctrlData))
        state.buttonFlags |= UP_FLAG;
    if (isPressed(mapping.btnDpadDown, ctrlData))
        state.buttonFlags |= DOWN_FLAG;
    if (isPressed(mapping.btnDpadLeft, ctrlData))
        state.buttonFlags |= LEFT_FLAG;
    if (isPressed(mapping.btnDpadRight, ctrlData))
        state.buttonFlags |= RIGHT_FLAG;

    // Botones de cara (la conversión se hace en el servidor según tipo reportado)
    if (isPressed(mapping.btnSouth, ctrlData))
        state.buttonFlags |= A_FLAG;
    if (isPressed(mapping.btnEast, ctrlData))
        state.buttonFlags |= B_FLAG;
    if (isPressed(mapping.btnWest, ctrlData))
        state.buttonFlags |= X_FLAG;
    if (isPressed(mapping.btnNorth, ctrlData))
        state.buttonFlags |= Y_FLAG;

    if (isPressed(mapping.btnStart, ctrlData))
        state.buttonFlags |= PLAY_FLAG;
    if (isPressed(mapping.btnSelect, ctrlData))
        state.buttonFlags |= BACK_FLAG;

    // L1/R1 se mapean a LB/RB (consistente con Xbox)
    if (isPressed(mapping.btnL1, ctrlData))
        state.buttonFlags |= LB_FLAG;
    if (isPressed(mapping.btnR1, ctrlData))
        state.buttonFlags |= RB_FLAG;
    state.leftTrigger  = readTrigger(mapping.btnL2, ctrlData);
    state.rightTrigger = readTrigger(mapping.btnR2, ctrlData);

    if (isPressed(mapping.btnL3, ctrlData))
        state.buttonFlags |= LS_CLK_FLAG;
    if (isPressed(mapping.btnR3, ctrlData))
        state.buttonFlags |= RS_CLK_FLAG;

    state.leftStickX  = readAxis(mapping.absLX, ctrlData);
    state.leftStickY  = (short)-readAxis(mapping.absLY, ctrlData);
    state.rightStickX = readAxis(mapping.absRX, ctrlData);
    state.rightStickY = (short)-readAxis(mapping.absRY, ctrlData);

    state.leftStickX  = applyDeadzone(state.leftStickX);
    state.leftStickY  = applyDeadzone(state.leftStickY);
    state.rightStickX = applyDeadzone(state.rightStickX);
    state.rightStickY = applyDeadzone(state.rightStickY);

    return state;
}

bool ControllerInputManager::isPressed(uint32_t binding, const SceCtrlData& pad) const
{
    if (binding == 0)
    {
        return false;
    }

    if ((binding & INPUT_TYPE_MASK) != INPUT_TYPE_GAMEPAD)
    {
        return false;
    }

    return (pad.buttons & (binding & INPUT_VALUE_MASK)) != 0;
}

unsigned char ControllerInputManager::readTrigger(uint32_t binding, const SceCtrlData& pad) const
{
    if (binding == 0)
    {
        return 0;
    }

    switch (binding & INPUT_TYPE_MASK)
    {
        case INPUT_TYPE_ANALOG:
        {
            switch (binding & INPUT_VALUE_MASK)
            {
                case ANALOG_LEFT_TRIGGER_BIND:
                    return pad.lt;
                case ANALOG_RIGHT_TRIGGER_BIND:
                    return pad.rt;
                default:
                    return 0;
            }
        }
        case INPUT_TYPE_GAMEPAD:
            return (pad.buttons & (binding & INPUT_VALUE_MASK)) ? 0xFF : 0;
        default:
            return 0;
    }
}

short ControllerInputManager::readAxis(uint32_t binding, const SceCtrlData& pad) const
{
    if ((binding & INPUT_TYPE_MASK) != INPUT_TYPE_ANALOG)
    {
        return 0;
    }

    int value = 128;
    switch (binding & INPUT_VALUE_MASK)
    {
        case ANALOG_LEFT_X:
            value = pad.lx;
            break;
        case ANALOG_LEFT_Y:
            value = pad.ly;
            break;
        case ANALOG_RIGHT_X:
            value = pad.rx;
            break;
        case ANALOG_RIGHT_Y:
            value = pad.ry;
            break;
        default:
            value = 128;
            break;
    }

    return static_cast<short>(value * 256 - 32640);
}

short ControllerInputManager::applyDeadzone(short value) const
{
    return (std::abs(value) < 1024) ? 0 : value;
}

void ControllerInputManager::applyRearTouchSettings(const RearTouchSettings& settings)
{
    if (rearTouchManager)
    {
        rearTouchManager->updateSettings(settings);
    }
}

void ControllerInputManager::setGamepadType(GamepadType type)
{
    if (currentGamepadType == type)
    {
        vita_debug_log("[ControllerInput] Gamepad type ya es %d, ignorando", (int)type);
        return;
    }

    currentGamepadType = type;
    vita_debug_log("[ControllerInput] Cambiando tipo de gamepad a %d (%s)",
        (int)type, (type == GAMEPAD_TYPE_PS4) ? "PS4" : "XBOX");

    // Para cambiar el tipo de un controlador existente, necesitamos desconectarlo y reconectarlo
    // porque Sunshine no soporta cambiar el tipo de controladores ya asignados

    // Paso 1: Desconectar el controlador enviando activeGamepadMask = 0
    vita_debug_log("[ControllerInput] Desconectando controlador para cambio de tipo...");
    if (LiSendMultiControllerEvent(0, 0, 0, 0, 0, 0, 0, 0, 0) != 0)
    {
        vita_debug_log("[ControllerInput][ERR] Fallo al desconectar controlador");
    }

    // Paso 2: Esperar un poco para que Sunshine procese la desconexión
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Paso 3: Reconectar con el nuevo tipo
    uint8_t liType                = (type == GAMEPAD_TYPE_PS4) ? 0x02 : 0x01;
    uint16_t capabilities         = 0x01 | 0x02; // ANALOG_TRIGGERS | RUMBLE
    uint32_t supportedButtonFlags = 0xFFFFFFFF; // Todos los botones

    vita_debug_log("[ControllerInput] Reconectando controlador con nuevo tipo (LI_CTYPE=%d)...", liType);
    if (LiSendControllerArrivalEvent(0, 0x01, liType, supportedButtonFlags, capabilities) != 0)
    {
        vita_debug_log("[ControllerInput][ERR] LiSendControllerArrivalEvent fallo");
    }
    else
    {
        vita_debug_log("[ControllerInput] Tipo de gamepad notificado al host (LI_CTYPE=%d)", liType);
    }

    // Enviar estado cero inmediato para resetear cualquier botón pendiente
    GamepadState zeroState = { 0 };
    sendGamepadState(zeroState);
}
void ControllerInputManager::setRearTouchEnabled(bool enabled)
{
    if (rearTouchManager)
    {
        rearTouchManager->setEnabled(enabled);
    }
}

// Validar y cambiar modo touch con compatibilidad de gamepad
bool ControllerInputManager::setTouchscreenModeWithValidation(int newMode)
{
    if (!touchManager)
    {
        vita_debug_log("[ControllerInput][ERR] TouchManager no inicializado");
        return false;
    }

    // Usar el método de validación de TouchInput
    if (touchManager->setTouchMode(newMode, static_cast<int>(currentGamepadType)))
    {
        touchscreenMode = newMode;
        vita_debug_log("[ControllerInput] Modo touch validado y cambiado a %d", newMode);
        return true;
    }
    else
    {
        vita_debug_log("[ControllerInput][WARN] Modo touch %d no compatible con gamepad tipo %d",
            newMode, static_cast<int>(currentGamepadType));
        return false;
    }
}

// Cambiar modo touch en tiempo de ejecución (guarda config y valida)
bool ControllerInputManager::setTouchscreenModeRuntime(int newMode)
{
    if (touchscreenMode == newMode)
    {
        vita_debug_log("[ControllerInput] Modo touch ya es %d, ignorando", newMode);
        return true;
    }

    if (!touchManager)
    {
        vita_debug_log("[ControllerInput][ERR] TouchManager no inicializado");
        return false;
    }

    // Validar compatibilidad con gamepad actual
    if (!touchManager->setTouchMode(newMode, static_cast<int>(currentGamepadType)))
    {
        vita_debug_log("[ControllerInput][WARN] Modo touch %d no compatible con gamepad tipo %d",
            newMode, static_cast<int>(currentGamepadType));
        return false;
    }

    // Actualizar modo actual
    touchscreenMode = newMode;
    vita_debug_log("[ControllerInput] Modo touch cambiado en tiempo de ejecución a %d", newMode);

    // Guardar en config para persistencia
    ConfigManager config;
    config.load();
    VideoSettings settings    = config.getVideoSettings();
    settings.touchscreen_mode = newMode;
    config.setVideoSettings(settings);
    config.save();

    return true;
}
