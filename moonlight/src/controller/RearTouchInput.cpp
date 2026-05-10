#include "controller/RearTouchInput.hpp"

#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <string.h>

#include <algorithm>

#include "Limelight.h"
#include "controller/input_types.hpp"
#include "controller/shortcuts.hpp"
#include "controller/special_inputs.hpp"

namespace
{
constexpr float PANEL_WIDTH  = 960.0f;
constexpr float PANEL_HEIGHT = 544.0f;
constexpr float MAX_TOUCH_X  = 1919.0f;
constexpr float MAX_TOUCH_Y  = 1087.0f;

inline float clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}
}

RearTouchInputManager::RearTouchInputManager()
{
    // Default values
    updateSettings(currentSettings);
    setEnabled(true);
}

void RearTouchInputManager::recalcBounds()
{
    leftNorm   = clamp01(static_cast<float>(currentSettings.left) / PANEL_WIDTH);
    rightNorm  = clamp01(1.0f - static_cast<float>(currentSettings.right) / PANEL_WIDTH);
    topNorm    = clamp01(static_cast<float>(currentSettings.top) / PANEL_HEIGHT);
    bottomNorm = clamp01(1.0f - static_cast<float>(currentSettings.bottom) / PANEL_HEIGHT);

    if (leftNorm > rightNorm)
    {
        std::swap(leftNorm, rightNorm);
    }
    if (topNorm > bottomNorm)
    {
        std::swap(topNorm, bottomNorm);
    }

    const float width  = std::max(rightNorm - leftNorm, 0.02f);
    const float height = std::max(bottomNorm - topNorm, 0.02f);

    midX = leftNorm + width * 0.5f;
    midY = topNorm + height * 0.5f;
}

void RearTouchInputManager::updateSettings(const RearTouchSettings& settings)
{
    currentSettings = settings;
    enabled         = currentSettings.enabled;
    recalcBounds();
    lastZoneActive.fill(false);
}

void RearTouchInputManager::setEnabled(bool value)
{
    enabled                 = value;
    currentSettings.enabled = value;
    if (!enabled)
    {
        lastZoneActive.fill(false);
    }
}

void RearTouchInputManager::setSwapShoulderButtons(bool value)
{
    if (swapShoulderButtons != value)
    {
        dropState();
    }
    swapShoulderButtons = value;
}

void RearTouchInputManager::process(GamepadState& state, bool isPstvModel)
{
    if (!enabled || isPstvModel)
    {
        return;
    }

    static uint64_t lastRearTouchPollUs = 0;
    uint64_t nowUs                      = sceKernelGetSystemTimeWide();
    // Poll rear touch at 125Hz to reduce CPU cost without noticeable latency.
    if (lastRearTouchPollUs != 0 && (nowUs - lastRearTouchPollUs) < 8000)
    {
        return;
    }
    lastRearTouchPollUs = nowUs;

    SceTouchData backData;
    memset(&backData, 0, sizeof(SceTouchData));
    sceTouchPeek(SCE_TOUCH_PORT_BACK, &backData, 1);

    std::array<bool, 4> zoneActive { false, false, false, false };

    for (int i = 0; i < backData.reportNum; ++i)
    {
        const SceTouchReport& report = backData.report[i];
        const float x                = clamp01(static_cast<float>(report.x) / MAX_TOUCH_X);
        const float y                = clamp01(static_cast<float>(report.y) / MAX_TOUCH_Y);

        if (x < leftNorm || x > rightNorm || y < topNorm || y > bottomNorm)
        {
            continue;
        }

        const bool leftSide = x < midX;
        const bool topSide  = y < midY;

        if (leftSide && topSide)
        {
            zoneActive[0] = true;
        }
        else if (!leftSide && topSide)
        {
            zoneActive[1] = true;
        }
        else if (leftSide && !topSide)
        {
            zoneActive[2] = true;
        }
        else
        {
            zoneActive[3] = true;
        }
    }

    for (std::size_t i = 0; i < zoneActive.size(); ++i)
    {
        std::uint32_t actionCode = 0;
        if (swapShoulderButtons)
        {
            // Fixed mapping when swap is active (legacy behavior):
            // NW (0) -> L1 digital, NE (1) -> R1 digital
            // SW (2) -> L3, SE (3) -> R3
            static const std::uint32_t swapActions[4] = {
                controller::INPUT_TYPE_GAMEPAD | LB_FLAG,
                controller::INPUT_TYPE_GAMEPAD | RB_FLAG,
                controller::INPUT_TYPE_GAMEPAD | LS_CLK_FLAG,
                controller::INPUT_TYPE_GAMEPAD | RS_CLK_FLAG
            };
            actionCode = swapActions[i];
        }
        else
        {
            actionCode = getActionForIndex(i);
        }
        handleZoneAction(i, zoneActive[i], actionCode, state);
    }
}

void RearTouchInputManager::dropState()
{
    GamepadState dummy {};
    for (std::size_t i = 0; i < lastZoneActive.size(); ++i)
    {
        if (lastZoneActive[i])
        {
            std::uint32_t actionCode = 0;
            if (swapShoulderButtons)
            {
                static const std::uint32_t swapActions[4] = {
                    controller::INPUT_TYPE_GAMEPAD | LB_FLAG,
                    controller::INPUT_TYPE_GAMEPAD | RB_FLAG,
                    controller::INPUT_TYPE_GAMEPAD | LS_CLK_FLAG,
                    controller::INPUT_TYPE_GAMEPAD | RS_CLK_FLAG
                };
                actionCode = swapActions[i];
            }
            else
            {
                actionCode = getActionForIndex(i);
            }
            handleZoneAction(i, false, actionCode, dummy);
        }
    }
    lastZoneActive.fill(false);
}

std::uint32_t RearTouchInputManager::getActionForIndex(std::size_t index) const
{
    switch (index)
    {
        case 0:
            return currentSettings.actionNorthWest;
        case 1:
            return currentSettings.actionNorthEast;
        case 2:
            return currentSettings.actionSouthWest;
        case 3:
            return currentSettings.actionSouthEast;
        default:
            return 0;
    }
}

void RearTouchInputManager::handleZoneAction(std::size_t index, bool pressed, std::uint32_t code, GamepadState& state)
{
    if (code == 0)
    {
        lastZoneActive[index] = pressed;
        return;
    }

    const std::uint32_t type  = code & controller::INPUT_TYPE_MASK;
    const std::uint32_t value = code & controller::INPUT_VALUE_MASK;

    switch (type)
    {
        case controller::INPUT_TYPE_SPECIAL:
        {
            if (pressed && !lastZoneActive[index])
            {
                trigger_virtual_shortcut(value);
            }
            break;
        }
        case controller::INPUT_TYPE_GAMEPAD:
        {
            if (pressed)
            {
                state.buttonFlags |= value;
            }
            else if (lastZoneActive[index])
            {
                state.buttonFlags &= ~value;
            }
            break;
        }
        case controller::INPUT_TYPE_ANALOG:
        {
            if (pressed)
            {
                if (value == controller::ANALOG_LEFT_TRIGGER)
                {
                    state.leftTrigger = 0xFF;
                }
                else if (value == controller::ANALOG_RIGHT_TRIGGER)
                {
                    state.rightTrigger = 0xFF;
                }
            }
            else if (lastZoneActive[index])
            {
                if (value == controller::ANALOG_LEFT_TRIGGER)
                {
                    state.leftTrigger = 0;
                }
                else if (value == controller::ANALOG_RIGHT_TRIGGER)
                {
                    state.rightTrigger = 0;
                }
            }
            break;
        }
        case controller::INPUT_TYPE_MOUSE:
        {
            if (pressed && !lastZoneActive[index])
            {
                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, value);
            }
            else if (!pressed && lastZoneActive[index])
            {
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, value);
            }
            break;
        }
        case controller::INPUT_TYPE_KEYBOARD:
        {
            if (pressed && !lastZoneActive[index])
            {
                LiSendKeyboardEvent(value, KEY_ACTION_DOWN, 0);
            }
            else if (!pressed && lastZoneActive[index])
            {
                LiSendKeyboardEvent(value, KEY_ACTION_UP, 0);
            }
            break;
        }
        default:
            break;
    }

    lastZoneActive[index] = pressed;
}
