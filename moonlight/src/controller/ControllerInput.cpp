#include "controller/ControllerInput.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/shellutil.h>
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
#include "controller/Gyro.hpp"
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

inline short quantizeAxis(short value)
{
    // Quantize to reduce tiny analog jitter that causes excessive network updates.
    constexpr short kStep = 2048;
    return static_cast<short>((value / kStep) * kStep);
}

}

ControllerInputManager* g_controllerInput = nullptr;

ControllerInputManager::ControllerInputManager()
    : inputEnabled(true)
    , inputDropped(false)
    , touchscreenMode(0)
    , touchManager(nullptr)
    , rearTouchManager(nullptr)
    , pauseCallback(nullptr)
    , activeKeyboard(nullptr)
    , activeKeyboardSeenOpen(false)
{

    memset(&lastGamepadState, 0, sizeof(GamepadState));
    memset(&lastMouseState, 0, sizeof(VitaMouseState));
    memset(&lastCtrlData, 0, sizeof(SceCtrlData));
    memset(&mapping, 0, sizeof(mapping));

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    touchManager      = new TouchInputManager();
    g_touchInput      = touchManager;
    rearTouchManager  = new RearTouchInputManager();
    frontTouchManager = new FrontTouchInputManager();

    ConfigManager config;
    config.load();
    VideoSettings initialSettings = config.getVideoSettings();
    rearTouchManager->updateSettings(initialSettings.rear_touch);
    rearTouchManager->setSwapShoulderButtons(initialSettings.swap_shoulder_buttons);
    frontTouchManager->updateSettings(initialSettings);

    currentGamepadType  = initialSettings.gamepad_type;
    swapShoulderButtons = initialSettings.swap_shoulder_buttons;
    capturePSButton     = initialSettings.capture_ps_button;

    isPstvModel = (sceKernelGetModel() == SCE_KERNEL_MODEL_VITATV);
    initMapping();

    gyroManager = new GyroManager();

    vita_log::info("[ControllerInput] Initialized (gamepad_type=%d)", (int)currentGamepadType);
}

ControllerInputManager::~ControllerInputManager()
{
    set_pause_callback({});
    set_keyboard_callback({});
    set_front_touch_callback({});
    if (psButtonLocked)
    {
        unlockPSButton();
    }
    if (activeKeyboard)
    {
        IKeyboard* keyboard = activeKeyboard;
        activeKeyboard      = nullptr;
        keyboard->close();
        if (!keyboard->selfDestructs())
        {
            delete keyboard;
        }
    }
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
    if (frontTouchManager)
    {
        delete frontTouchManager;
        frontTouchManager = nullptr;
    }
    if (gyroManager)
    {
        delete gyroManager;
        gyroManager = nullptr;
    }
}

void ControllerInputManager::handleInput()
{
    using namespace std::chrono;
    if (!inputEnabled)
        return;

    static uint64_t lastInputPollUs = 0;
    const uint64_t nowPollUs        = (uint64_t)duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count();
    // Limit polling to 125Hz to reduce CPU contention with video decode/render.
    // This still keeps input latency low while freeing cycles for the 60 FPS target.
    if (lastInputPollUs != 0 && (nowPollUs - lastInputPollUs) < 8000)
    {
        return;
    }
    lastInputPollUs = nowPollUs;

    inputDropped = false;
    auto t_start = high_resolution_clock::now();

    SceCtrlData ctrlData;
    sceCtrlPeekBufferPositive(0, &ctrlData, 1);

    if (!isPstvModel)
    {
        // On the portable PS Vita, the physical L/R buttons appear as LTRIGGER/RTRIGGER.
        // We normalize so that they also activate the L1/R1 flags required by the shortcuts.
        if (ctrlData.buttons & SCE_CTRL_LTRIGGER)
        {
            ctrlData.buttons |= SCE_CTRL_L1;
        }
        if (ctrlData.buttons & SCE_CTRL_RTRIGGER)
        {
            ctrlData.buttons |= SCE_CTRL_R1;
        }
    }

    static int debug_counter = 0;
    if (debug_counter++ % 300 == 0)
    { // Approximately every 2-3 seconds (depends on poll rate)
        vita_log::info("[ControllerInput] Botones: 0x%08X (L1:%d R1:%d L2:%d R2:%d START:%d) LT:%d RT:%d LX:%d LY:%d RX:%d RY:%d",
            ctrlData.buttons,
            (ctrlData.buttons & SCE_CTRL_L1) ? 1 : 0,
            (ctrlData.buttons & SCE_CTRL_R1) ? 1 : 0,
            (ctrlData.buttons & SCE_CTRL_L2) ? 1 : 0,
            (ctrlData.buttons & SCE_CTRL_R2) ? 1 : 0,
            (ctrlData.buttons & SCE_CTRL_START) ? 1 : 0,
            ctrlData.lt,
            ctrlData.rt,
            ctrlData.lx,
            ctrlData.ly,
            ctrlData.rx,
            ctrlData.ry);
    }

    if (process_physical_shortcuts(&ctrlData, &lastCtrlData))
    {
        vita_log::info("[ControllerInput] Shortcut ejecutado, retornando temprano");
        lastCtrlData = ctrlData;
        return;
    }

    GamepadState gamepadState = buildGamepadState(ctrlData);

    // Outside a stream, the system handles the PS button normally.
    if (streamingActive && capturePSButton)
    {
        constexpr uint64_t PSBTN_DOUBLETAP_DELAY_US = 500000; // 500ms
        const bool psButtonPressed                  = (ctrlData.buttons & SCE_CTRL_PSBUTTON) != 0;
        const bool psButtonWasPressed               = (lastCtrlData.buttons & SCE_CTRL_PSBUTTON) != 0;

        if (psButtonPressed)
        {
            if (!psButtonWasPressed)
            {
                if ((nowPollUs - psButtonPressedTimeUs) < PSBTN_DOUBLETAP_DELAY_US)
                {
                    vita_log::info("[PSBTN] Double-tap detected -> unlock");
                    unlockPSButton();
                    psButtonSpecialActive = false;
                }
                else
                {
                    vita_log::info("[PSBTN] Single tap -> SPECIAL active");
                    psButtonSpecialActive = true;
                }
            }
            else
            {
                if (psButtonLocked)
                {
                    psButtonSpecialActive = true; // locked: keep SPECIAL active
                }
                else
                {
                    psButtonSpecialActive = false; // unlocked: let Vita OS handle it
                }
            }
            psButtonPressedTimeUs = nowPollUs; // update EVERY frame while pressed
        }
        else
        {
            if (psButtonWasPressed)
            {
                vita_log::info("[PSBTN] Released -> SPECIAL inactive");
                psButtonSpecialActive = false;
            }

            if (!psButtonLocked && (nowPollUs - psButtonPressedTimeUs) > PSBTN_DOUBLETAP_DELAY_US)
            {
                vita_log::info("[PSBTN] Auto re-lock after delay");
                lockPSButton(); // re-lock after delay since last release
            }
        }

        if (psButtonSpecialActive)
        {
            gamepadState.buttonFlags |= SPECIAL_FLAG;
        }
    }
    else
    {
        psButtonSpecialActive = false;
    }
    if (rearTouchManager)
    {
        rearTouchManager->process(gamepadState, isPstvModel);
    }

    if (activeKeyboard)
    {
        activeKeyboard->update();
    }

    bool inFrontTouchZone = false;
    bool keyboardOpen     = (activeKeyboard && activeKeyboard->isOpen());
    if (frontTouchManager && !keyboardOpen)
    {
        inFrontTouchZone = frontTouchManager->process(gamepadState, isPstvModel);
    }

    // Send if changed. Button/trigger changes go out immediately; analog-only
    // changes are rate-limited to avoid CPU/network spikes from stick jitter.
    const bool stateChanged          = (memcmp(&gamepadState, &lastGamepadState, sizeof(GamepadState)) != 0);
    static uint64_t lastAnalogSendUs = 0;
    static GamepadState pendingAnalogState {};
    static bool hasPendingAnalogState = false;

    const uint64_t nowUs                        = (uint64_t)duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count();
    constexpr uint64_t kMinAnalogSendIntervalUs = 8000; // 125Hz

    if (stateChanged)
    {
        const bool digitalChanged = gamepadState.buttonFlags != lastGamepadState.buttonFlags || gamepadState.leftTrigger != lastGamepadState.leftTrigger || gamepadState.rightTrigger != lastGamepadState.rightTrigger;

        if (digitalChanged || lastAnalogSendUs == 0 || (nowUs - lastAnalogSendUs) >= kMinAnalogSendIntervalUs)
        {
            sendGamepadState(gamepadState);
            lastGamepadState      = gamepadState;
            lastAnalogSendUs      = nowUs;
            hasPendingAnalogState = false;
        }
        else
        {
            pendingAnalogState    = gamepadState;
            hasPendingAnalogState = true;
        }
    }
    else if (hasPendingAnalogState && (nowUs - lastAnalogSendUs) >= kMinAnalogSendIntervalUs)
    {
        sendGamepadState(pendingAnalogState);
        lastGamepadState      = pendingAnalogState;
        lastAnalogSendUs      = nowUs;
        hasPendingAnalogState = false;
    }

    lastCtrlData = ctrlData;

    if (gyroManager)
    {
        gyroManager->update();
    }

    if (touchManager)
    {
        constexpr uint64_t kTouchReleaseDelayUs = 150000;
        const bool keyboardOpen                 = (activeKeyboard != nullptr && activeKeyboard->isOpen());
        const bool keyboardStreamTouch          = keyboardOpen && activeKeyboard->allowsStreamTouch() && activeKeyboard->streamTouchHeight() > 0;
        const bool keyboardBlocksStreamTouch    = keyboardStreamTouch && activeKeyboard->blocksStreamTouch();
        const bool keyboardModeChanged          = keyboardOpen != lastKeyboardOpen || keyboardStreamTouch != lastKeyboardStreamTouch;

        if (keyboardOpen && (!keyboardStreamTouch || keyboardBlocksStreamTouch))
        {
            if (!touchSuppressed)
            {
                touchManager->dropTouch(touchscreenMode);
                touchSuppressed = true;
            }
            touchSuppressUntilUs = nowUs + kTouchReleaseDelayUs;
        }
        else
        {
            if (keyboardModeChanged)
            {
                touchManager->dropTouch(touchscreenMode);
                touchSuppressed      = true;
                touchSuppressUntilUs = nowUs + kTouchReleaseDelayUs;
            }

            if (touchSuppressed && nowUs < touchSuppressUntilUs)
            {
            }
            else if (!inFrontTouchZone)
            {
                touchSuppressed        = false;
                const int activeHeight = keyboardStreamTouch ? activeKeyboard->streamTouchHeight() : 544;
                touchManager->handleTouch(touchscreenMode, activeHeight);
            }
            else
            {
                touchSuppressed = true;
                touchManager->dropTouch(touchscreenMode);
                touchSuppressUntilUs = nowUs + kTouchReleaseDelayUs;
            }
        }

        lastKeyboardOpen        = keyboardOpen;
        lastKeyboardStreamTouch = keyboardStreamTouch;
    }

    if (activeKeyboard)
    {
        if (activeKeyboard->isOpen())
        {
            activeKeyboardSeenOpen = true;
        }

        // This avoids destroying legacy keyboard while its IME thread is still starting.
        if (activeKeyboardSeenOpen && !activeKeyboard->isOpen())
        {
            vita_log::info("[ControllerInput] Active keyboard closed itself, cleaning up");
            IKeyboard* kb = activeKeyboard;
            setActiveKeyboard(nullptr);
            if (!kb->selfDestructs())
            {
                delete kb;
            }
        }
    }

    if (activeKeyboard && !activeKeyboard->sendsDirectly())
    {
        static KeyboardState oldKeyboardState;
        KeyboardState keyboardState = activeKeyboard->getKeyboardState();
        const char kbFlags          = activeKeyboard->usesNonNormalizedVk() ? SS_KBE_FLAG_NON_NORMALIZED : 0;

        for (int i = 0; i < 256; ++i)
        {
            if (keyboardState.keys[i] != oldKeyboardState.keys[i])
            {
                oldKeyboardState.keys[i] = keyboardState.keys[i];
                LiSendKeyboardEvent2(
                    (short)i,
                    keyboardState.keys[i] ? KEY_ACTION_DOWN : KEY_ACTION_UP,
                    0,
                    kbFlags);
                vita_log::info("[ControllerInput] Keyboard VK 0x%02X -> %s",
                    i, keyboardState.keys[i] ? "DOWN" : "UP");
            }
        }
    }

    auto t_end                     = high_resolution_clock::now();
    auto dur_us                    = duration_cast<microseconds>(t_end - t_start).count();
    uint64_t now_ms                = duration_cast<milliseconds>(t_end.time_since_epoch()).count();
    static uint64_t lastInputLogMs = 0;
    if (now_ms - lastInputLogMs > 2000)
    {
        lastInputLogMs = now_ms;
        vita_log::debug("[ControllerInput][PERF] handleInput time=%lld us inputEnabled=%d inputDropped=%d", (long long)dur_us, inputEnabled ? 1 : 0, inputDropped ? 1 : 0);
    }
}

void ControllerInputManager::sendGamepadState(const GamepadState& state)
{
    if (LiSendMultiControllerEvent(0, 1, state.buttonFlags, state.leftTrigger, state.rightTrigger,
            state.leftStickX, state.leftStickY, state.rightStickX, state.rightStickY)
        != 0)
    {
        vita_log::error("[ControllerInput] Failed to send gamepad state");
    }
}

void ControllerInputManager::dropInput()
{
    if (inputDropped)
        return;

    GamepadState zeroState = { 0 };
    sendGamepadState(zeroState);

    touchManager->dropTouch(touchscreenMode);
    if (rearTouchManager)
    {
        rearTouchManager->dropState();
    }
    if (frontTouchManager)
    {
        frontTouchManager->dropState();
    }

    inputDropped = true;
    vita_log::info("[ControllerInput] Input dropped");
}

void ControllerInputManager::setPauseCallback(const std::function<void()>& cb)
{
    pauseCallback = cb;
    vita_log::info("[ControllerInput] setPauseCallback llamado, configurando callback");
    set_pause_callback(cb);
}

void ControllerInputManager::setKeyboardShortcutCallback(const std::function<void()>& cb)
{
    set_keyboard_callback(cb);
}

void ControllerInputManager::setFrontTouchShortcutCallback(const std::function<void()>& cb)
{
    set_front_touch_callback(cb);
}

void ControllerInputManager::setActiveKeyboard(IKeyboard* kb)
{
    vita_log::info("[ControllerInput] setActiveKeyboard: %p", kb);
    activeKeyboard         = kb;
    activeKeyboardSeenOpen = (kb != nullptr && kb->isOpen());
    if (kb)
    {
        unlockPSButton();
    }
    else if (streamingActive && inputEnabled && capturePSButton)
    {
        lockPSButton();
    }
}

// Public setter to enable/disable input processing.
// When disabled, an immediate dropInput() is done to send zero status
// to the host and prevent buttons being pressed in the transmission.
void ControllerInputManager::setInputEnabled(bool enabled)
{
    this->inputEnabled = enabled;
    if (!enabled)
    {
        dropInput();
        unlockPSButton();
    }
    else if (streamingActive && capturePSButton && activeKeyboard == nullptr)
    {
        lockPSButton();
    }
    vita_log::info("[ControllerInput] setInputEnabled -> %d", enabled ? 1 : 0);
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
        // There are no physical analog triggers on the portable PS Vita
        mapping.btnL2 = 0;
        mapping.btnR2 = 0;
    }
}

GamepadState ControllerInputManager::buildGamepadState(const SceCtrlData& ctrlData) const
{
    GamepadState state {};

    if (isPressed(mapping.btnDpadUp, ctrlData))
        state.buttonFlags |= UP_FLAG;
    if (isPressed(mapping.btnDpadDown, ctrlData))
        state.buttonFlags |= DOWN_FLAG;
    if (isPressed(mapping.btnDpadLeft, ctrlData))
        state.buttonFlags |= LEFT_FLAG;
    if (isPressed(mapping.btnDpadRight, ctrlData))
        state.buttonFlags |= RIGHT_FLAG;

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

    if (swapShoulderButtons)
    {
        if (ctrlData.buttons & SCE_CTRL_L1)
            state.leftTrigger = 0xFF;
        if (ctrlData.buttons & SCE_CTRL_R1)
            state.rightTrigger = 0xFF;
        // On PSTV, physical L2/R2 act as L1/R1 (digital).
        // On Vita normal, SCE_CTRL_L2/LR2 alias LTRIGGER/RTRIGGER (same physical buttons),
        // so L1/R1 digital must come from rear touch only (handled by RearTouchInputManager).
        if (isPstvModel)
        {
            if (ctrlData.buttons & SCE_CTRL_L2)
                state.buttonFlags |= LB_FLAG;
            if (ctrlData.buttons & SCE_CTRL_R2)
                state.buttonFlags |= RB_FLAG;
        }
    }
    else
    {
        if (isPressed(mapping.btnL1, ctrlData))
            state.buttonFlags |= LB_FLAG;
        if (isPressed(mapping.btnR1, ctrlData))
            state.buttonFlags |= RB_FLAG;
        state.leftTrigger  = readTrigger(mapping.btnL2, ctrlData);
        state.rightTrigger = readTrigger(mapping.btnR2, ctrlData);
    }

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
    if (std::abs(value) < 2048)
    {
        return 0;
    }
    return quantizeAxis(value);
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
        vita_log::info("[ControllerInput] Gamepad type ya es %d, ignorando", (int)type);
        return;
    }

    currentGamepadType = type;
    vita_log::info("[ControllerInput] Cambiando tipo de gamepad a %d (%s)",
        (int)type, (type == GAMEPAD_TYPE_PS4) ? "PS4" : "XBOX");

    // Sunshine applies controller-type changes after the next connection.
    vita_log::info("[ControllerInput] Disconnecting controller to change type...");
    if (LiSendMultiControllerEvent(0, 0, 0, 0, 0, 0, 0, 0, 0) != 0)
    {
        vita_log::error("[ControllerInput][ERR] Failed to disconnect controller");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint8_t liType        = (type == GAMEPAD_TYPE_PS4) ? 0x02 : 0x01;
    uint16_t capabilities = LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE;
    if (type == GAMEPAD_TYPE_PS4)
    {
        capabilities |= LI_CCAP_TOUCHPAD;
    }
    uint32_t supportedButtonFlags = 0xFFFFFFFF; // All buttons

    vita_log::info("[ControllerInput] Reconectando controlador con nuevo tipo (LI_CTYPE=%d)...", liType);
    if (LiSendControllerArrivalEvent(0, 0x01, liType, supportedButtonFlags, capabilities) != 0)
    {
        vita_log::error("[ControllerInput][ERR] LiSendControllerArrivalEvent failed");
    }
    else
    {
        vita_log::info("[ControllerInput] Tipo de gamepad notificado al host (LI_CTYPE=%d)", liType);
    }

    GamepadState zeroState = { 0 };
    sendGamepadState(zeroState);
}

void ControllerInputManager::setTouchscreenMode(int mode)
{
    if (mode < TOUCHSCREEN_MODE_OFF || mode > TOUCHSCREEN_MODE_TABLET)
    {
        mode = TOUCHSCREEN_MODE_TRACKPAD;
    }

    if (!touchManager || !touchManager->setTouchMode(mode, static_cast<int>(currentGamepadType)))
    {
        mode = TOUCHSCREEN_MODE_TRACKPAD;
        if (touchManager)
        {
            touchManager->setTouchMode(mode, static_cast<int>(currentGamepadType));
        }
    }
    touchscreenMode = mode;
}

void ControllerInputManager::setRearTouchEnabled(bool enabled)
{
    if (rearTouchManager)
    {
        rearTouchManager->setEnabled(enabled);
    }
}

void ControllerInputManager::setSwapShoulderButtons(bool enabled)
{
    swapShoulderButtons = enabled;
    vita_log::info("[ControllerInput] Swap shoulder buttons -> %d", enabled ? 1 : 0);
    if (rearTouchManager)
    {
        rearTouchManager->setSwapShoulderButtons(enabled);
    }
}

void ControllerInputManager::applyFrontTouchSettings(const VideoSettings& settings)
{
    if (frontTouchManager)
    {
        frontTouchManager->updateSettings(settings);
    }
}

void ControllerInputManager::setFrontTouchEnabled(bool enabled)
{
    if (frontTouchManager)
    {
        frontTouchManager->setEnabled(enabled);
    }
}

bool ControllerInputManager::toggleFrontTouchEnabled()
{
    if (!frontTouchManager)
    {
        return false;
    }

    const bool enabled = !frontTouchManager->isEnabled();
    frontTouchManager->setEnabled(enabled);
    vita_log::info("[ControllerInput] Front touch zones -> %d", enabled ? 1 : 0);
    return enabled;
}

bool ControllerInputManager::setTouchscreenModeWithValidation(int newMode)
{
    if (!touchManager)
    {
        vita_log::error("[ControllerInput][ERR] TouchManager no inicializado");
        return false;
    }

    if (touchManager->setTouchMode(newMode, static_cast<int>(currentGamepadType)))
    {
        touchscreenMode = newMode;
        vita_log::info("[ControllerInput] Modo touch validado y cambiado a %d", newMode);
        return true;
    }
    else
    {
        vita_log::warning("[ControllerInput][WARN] Modo touch %d no compatible con gamepad tipo %d",
            newMode, static_cast<int>(currentGamepadType));
        return false;
    }
}

bool ControllerInputManager::setTouchscreenModeRuntime(int newMode)
{
    if (touchscreenMode == newMode)
    {
        vita_log::info("[ControllerInput] Modo touch ya es %d, ignorando", newMode);
        return true;
    }

    if (!touchManager)
    {
        vita_log::error("[ControllerInput][ERR] TouchManager no inicializado");
        return false;
    }

    if (!touchManager->setTouchMode(newMode, static_cast<int>(currentGamepadType)))
    {
        vita_log::warning("[ControllerInput][WARN] Modo touch %d no compatible con gamepad tipo %d",
            newMode, static_cast<int>(currentGamepadType));
        return false;
    }

    touchscreenMode = newMode;
    vita_log::info("[ControllerInput] Touch mode changed at runtime to %d", newMode);

    ConfigManager config;
    config.updateVideoSettings([newMode](VideoSettings& settings)
        { settings.touchscreen_mode = newMode; });

    return true;
}

void ControllerInputManager::lockPSButton()
{
#if defined(__PSV__)
    if (capturePSButton && !psButtonLocked)
    {
        sceShellUtilLock((SceShellUtilLockType)(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN | SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2));
        psButtonLocked = true;
    }
#endif
}

void ControllerInputManager::unlockPSButton()
{
#if defined(__PSV__)
    if (psButtonLocked)
    {
        sceShellUtilUnlock((SceShellUtilLockType)(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN | SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2));
        psButtonLocked = false;
    }
#endif
}

void ControllerInputManager::setStreamingActive(bool active)
{
    streamingActive = active;
    if (active && capturePSButton && inputEnabled && activeKeyboard == nullptr)
    {
        lockPSButton();
    }
    else
    {
        psButtonSpecialActive = false;
        if (psButtonLocked)
        {
            unlockPSButton();
        }
    }
}

void ControllerInputManager::setPSButtonCapture(bool enabled)
{
    capturePSButton = enabled;
    if (enabled && streamingActive && inputEnabled && activeKeyboard == nullptr)
    {
        lockPSButton();
    }
    else
    {
        psButtonSpecialActive = false;
        unlockPSButton();
    }
}
