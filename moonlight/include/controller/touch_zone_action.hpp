#pragma once

#include <cstdint>

#include "controller/GamepadState.hpp"

namespace controller
{

void applyTouchZoneAction(bool pressed, bool wasPressed, std::uint32_t code, GamepadState& state);

}
