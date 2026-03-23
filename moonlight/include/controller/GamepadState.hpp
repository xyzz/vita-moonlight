#pragma once

#include <stdint.h>

// Represents the state of the gamepad to be sent to Limelight.
struct GamepadState
{
    uint16_t buttonFlags;
    unsigned char leftTrigger;
    unsigned char rightTrigger;
    short leftStickX;
    short leftStickY;
    short rightStickX;
    short rightStickY;
};
