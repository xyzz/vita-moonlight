#include "controller/touch_zone_action.hpp"

#include "Limelight.h"
#include "controller/input_types.hpp"
#include "controller/shortcuts.hpp"

namespace controller
{

void applyTouchZoneAction(bool pressed, bool wasPressed, std::uint32_t code, GamepadState& state)
{
    if (code == 0)
        return;

    const std::uint32_t type  = code & INPUT_TYPE_MASK;
    const std::uint32_t value = code & INPUT_VALUE_MASK;

    switch (type)
    {
        case INPUT_TYPE_SPECIAL:
            if (pressed && !wasPressed)
                trigger_virtual_shortcut(value);
            break;
        case INPUT_TYPE_GAMEPAD:
            if (pressed)
                state.buttonFlags |= value;
            else if (wasPressed)
                state.buttonFlags &= ~value;
            break;
        case INPUT_TYPE_ANALOG:
            if (value == ANALOG_LEFT_TRIGGER)
            {
                if (pressed)
                    state.leftTrigger = 0xff;
                else if (wasPressed)
                    state.leftTrigger = 0;
            }
            else if (value == ANALOG_RIGHT_TRIGGER)
            {
                if (pressed)
                    state.rightTrigger = 0xff;
                else if (wasPressed)
                    state.rightTrigger = 0;
            }
            break;
        case INPUT_TYPE_MOUSE:
            if (pressed != wasPressed)
                LiSendMouseButtonEvent(pressed ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE, value);
            break;
        case INPUT_TYPE_KEYBOARD:
            if (pressed != wasPressed)
                LiSendKeyboardEvent(value, pressed ? KEY_ACTION_DOWN : KEY_ACTION_UP, 0);
            break;
        default:
            break;
    }
}

}
