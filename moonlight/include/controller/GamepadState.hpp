#pragma once

#include <stdint.h>

// Representa el estado del gamepad a enviar hacia Limelight.
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
