#include "controller/FrontTouchInput.hpp"

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

FrontTouchInputManager::FrontTouchInputManager()
{
    recalcZones();
}

void FrontTouchInputManager::updateSettings(const VideoSettings& settings)
{
    enabled  = settings.enable_front_touchzones;
    offset   = settings.front_touch_offset;
    size     = settings.front_touch_size;
    actionNW = settings.front_action_northwest;
    actionNE = settings.front_action_northeast;
    actionSW = settings.front_action_southwest;
    actionSE = settings.front_action_southeast;
    recalcZones();
    lastZoneActive.fill(false);
}

void FrontTouchInputManager::setEnabled(bool value)
{
    enabled = value;
    if (!enabled)
    {
        lastZoneActive.fill(false);
    }
}

void FrontTouchInputManager::recalcZones()
{
    // NW: top-left corner
    zones[0].left   = static_cast<float>(offset);
    zones[0].top    = static_cast<float>(offset);
    zones[0].right  = static_cast<float>(offset + size);
    zones[0].bottom = static_cast<float>(offset + size);

    // NE: top-right corner
    zones[1].left   = PANEL_WIDTH - static_cast<float>(offset + size);
    zones[1].top    = static_cast<float>(offset);
    zones[1].right  = PANEL_WIDTH - static_cast<float>(offset);
    zones[1].bottom = static_cast<float>(offset + size);

    // SW: bottom-left corner
    zones[2].left   = static_cast<float>(offset);
    zones[2].top    = PANEL_HEIGHT - static_cast<float>(offset + size);
    zones[2].right  = static_cast<float>(offset + size);
    zones[2].bottom = PANEL_HEIGHT - static_cast<float>(offset);

    // SE: bottom-right corner
    zones[3].left   = PANEL_WIDTH - static_cast<float>(offset + size);
    zones[3].top    = PANEL_HEIGHT - static_cast<float>(offset + size);
    zones[3].right  = PANEL_WIDTH - static_cast<float>(offset);
    zones[3].bottom = PANEL_HEIGHT - static_cast<float>(offset);
}

bool FrontTouchInputManager::process(GamepadState& state, bool isPstvModel)
{
    if (!enabled || isPstvModel)
    {
        return false;
    }

    static uint64_t lastFrontTouchPollUs = 0;
    uint64_t nowUs                       = sceKernelGetSystemTimeWide();
    // Poll front touch at 125Hz to reduce CPU cost without noticeable latency.
    if (lastFrontTouchPollUs != 0 && (nowUs - lastFrontTouchPollUs) < 8000)
    {
        return false;
    }
    lastFrontTouchPollUs = nowUs;

    SceTouchData frontData;
    memset(&frontData, 0, sizeof(SceTouchData));
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &frontData, 1);

    std::array<bool, 4> zoneActive { false, false, false, false };
    bool anyInZone = false;

    for (int i = 0; i < frontData.reportNum; ++i)
    {
        const SceTouchReport& report = frontData.report[i];
        const float x                = clamp01(static_cast<float>(report.x) / MAX_TOUCH_X) * PANEL_WIDTH;
        const float y                = clamp01(static_cast<float>(report.y) / MAX_TOUCH_Y) * PANEL_HEIGHT;

        for (std::size_t z = 0; z < zones.size(); ++z)
        {
            if (x >= zones[z].left && x <= zones[z].right && y >= zones[z].top && y <= zones[z].bottom)
            {
                zoneActive[z] = true;
                anyInZone     = true;
            }
        }
    }

    for (std::size_t i = 0; i < zoneActive.size(); ++i)
    {
        const std::uint32_t actionCode = getActionForIndex(i);
        handleZoneAction(i, zoneActive[i], actionCode, state);
    }

    return anyInZone;
}

void FrontTouchInputManager::dropState()
{
    GamepadState dummy {};
    for (std::size_t i = 0; i < lastZoneActive.size(); ++i)
    {
        if (lastZoneActive[i])
        {
            handleZoneAction(i, false, getActionForIndex(i), dummy);
        }
    }
    lastZoneActive.fill(false);
}

std::uint32_t FrontTouchInputManager::getActionForIndex(std::size_t index) const
{
    switch (index)
    {
        case 0:
            return actionNW;
        case 1:
            return actionNE;
        case 2:
            return actionSW;
        case 3:
            return actionSE;
        default:
            return 0;
    }
}

void FrontTouchInputManager::handleZoneAction(std::size_t index, bool pressed, std::uint32_t code, GamepadState& state)
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
