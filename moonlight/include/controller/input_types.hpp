#pragma once

#include <cstdint>

namespace controller
{

inline constexpr std::uint32_t INPUT_TYPE_MASK  = 0xfff00000;
inline constexpr std::uint32_t INPUT_VALUE_MASK = 0x000fffff;

inline constexpr std::uint32_t INPUT_TYPE_KEYBOARD    = 0x00000000;
inline constexpr std::uint32_t INPUT_TYPE_SPECIAL     = 0x00100000;
inline constexpr std::uint32_t INPUT_TYPE_MOUSE       = 0x00200000;
inline constexpr std::uint32_t INPUT_TYPE_GAMEPAD     = 0x00300000;
inline constexpr std::uint32_t INPUT_TYPE_ANALOG      = 0x00400000;
inline constexpr std::uint32_t INPUT_TYPE_TOUCHSCREEN = 0x00500000;
inline constexpr std::uint32_t INPUT_TYPE_DEF_NAME    = 0xf0000000;

inline constexpr std::uint32_t ANALOG_LEFT_TRIGGER  = 4;
inline constexpr std::uint32_t ANALOG_RIGHT_TRIGGER = 5;

inline constexpr std::uint32_t GAMEPAD_FLAG_LB      = 0x0100;
inline constexpr std::uint32_t GAMEPAD_FLAG_RB      = 0x0200;
inline constexpr std::uint32_t GAMEPAD_FLAG_LS      = 0x0040;
inline constexpr std::uint32_t GAMEPAD_FLAG_RS      = 0x0080;
inline constexpr std::uint32_t GAMEPAD_FLAG_SPECIAL = 0x0400;

inline constexpr std::uint32_t MOUSE_BUTTON_LEFT   = 0x01;
inline constexpr std::uint32_t MOUSE_BUTTON_MIDDLE = 0x02;
inline constexpr std::uint32_t MOUSE_BUTTON_RIGHT  = 0x03;
inline constexpr std::uint32_t MOUSE_BUTTON_X1     = 0x04;
inline constexpr std::uint32_t MOUSE_BUTTON_X2     = 0x05;

} // namespace controller
