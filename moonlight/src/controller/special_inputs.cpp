#include "controller/special_inputs.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

#include "controller/input_types.hpp"

namespace controller
{
namespace
{

    using OptionArray = std::array<SpecialInputOption, 35>;

    constexpr SpecialInputOption makeHeader(std::uint32_t type, const char* name)
    {
        return { INPUT_TYPE_DEF_NAME | type, name, false };
    }

    constexpr SpecialInputOption makeSelectable(std::uint32_t code, const char* name)
    {
        return { code, name, true };
    }

    constexpr OptionArray buildOptions()
    {
        return OptionArray { { makeSelectable(0, "None"),
            makeHeader(INPUT_TYPE_SPECIAL, "Special inputs"),
            makeSelectable(INPUT_TYPE_SPECIAL | INPUT_SPECIAL_KEY_PAUSE, "Pause stream"),
            makeSelectable(INPUT_TYPE_SPECIAL | INPUT_SPECIAL_KEY_KEYBOARD, "Open keyboard"),
            makeHeader(INPUT_TYPE_GAMEPAD, "Gamepad buttons"),
            makeSelectable(INPUT_TYPE_GAMEPAD | GAMEPAD_FLAG_SPECIAL, "Special (XBox button)"),
            makeSelectable(INPUT_TYPE_GAMEPAD | GAMEPAD_FLAG_LB, "L1"),
            makeSelectable(INPUT_TYPE_GAMEPAD | GAMEPAD_FLAG_RB, "R1"),
            makeSelectable(INPUT_TYPE_GAMEPAD | GAMEPAD_FLAG_LS, "L3"),
            makeSelectable(INPUT_TYPE_GAMEPAD | GAMEPAD_FLAG_RS, "R3"),
            makeSelectable(INPUT_TYPE_ANALOG | ANALOG_LEFT_TRIGGER, "L2"),
            makeSelectable(INPUT_TYPE_ANALOG | ANALOG_RIGHT_TRIGGER, "R2"),
            makeHeader(INPUT_TYPE_MOUSE, "Mouse buttons"),
            makeSelectable(INPUT_TYPE_MOUSE | MOUSE_BUTTON_LEFT, "Left"),
            makeSelectable(INPUT_TYPE_MOUSE | MOUSE_BUTTON_RIGHT, "Right"),
            makeSelectable(INPUT_TYPE_MOUSE | MOUSE_BUTTON_MIDDLE, "Middle(wheel)"),
            makeSelectable(INPUT_TYPE_MOUSE | MOUSE_BUTTON_X1, "X1(4th)"),
            makeSelectable(INPUT_TYPE_MOUSE | MOUSE_BUTTON_X2, "X2(5th)"),
            makeHeader(INPUT_TYPE_KEYBOARD, "Keyboard input codes"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 27, "Esc"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 73, "I"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 77, "M"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 9, "Tab"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 112, "F1"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 113, "F2"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 114, "F3"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 115, "F4"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 116, "F5"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 117, "F6"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 118, "F7"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 119, "F8"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 120, "F9"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 121, "F10"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 122, "F11"),
            makeSelectable(INPUT_TYPE_KEYBOARD | 123, "F12") } };
    }

    const OptionArray& getOptionArray()
    {
        static const OptionArray options = buildOptions();
        return options;
    }

    const std::vector<SpecialInputOption>& getAllOptionsVector()
    {
        static const std::vector<SpecialInputOption> options(getOptionArray().begin(), getOptionArray().end());
        return options;
    }

    const std::vector<SpecialInputOption>& getSelectableOptionsVector()
    {
        static const std::vector<SpecialInputOption> options = []
        {
            std::vector<SpecialInputOption> filtered;
            filtered.reserve(getOptionArray().size());
            for (const auto& option : getOptionArray())
            {
                if (option.selectable)
                {
                    filtered.push_back(option);
                }
            }
            return filtered;
        }();
        return options;
    }

} // namespace

const std::vector<SpecialInputOption>& getAllSpecialInputOptions()
{
    return getAllOptionsVector();
}

const std::vector<SpecialInputOption>& getSelectableSpecialInputOptions()
{
    return getSelectableOptionsVector();
}

const SpecialInputOption* findSpecialInputOption(std::uint32_t code)
{
    const auto& options = getAllOptionsVector();
    auto it             = std::find_if(options.begin(), options.end(), [code](const SpecialInputOption& option)
                    { return option.code == code; });
    if (it != options.end())
    {
        return &(*it);
    }
    return nullptr;
}

std::size_t getSelectableIndexForCode(std::uint32_t code)
{
    const auto& selectable = getSelectableOptionsVector();
    for (std::size_t i = 0; i < selectable.size(); ++i)
    {
        if (selectable[i].code == code)
        {
            return i;
        }
    }
    return 0;
}

std::uint32_t getCodeForSelectableIndex(std::size_t index)
{
    const auto& selectable = getSelectableOptionsVector();
    if (index < selectable.size())
    {
        return selectable[index].code;
    }
    return selectable.empty() ? 0 : selectable.front().code;
}

std::string getDisplayNameForCode(std::uint32_t code)
{
    if (const auto* option = findSpecialInputOption(code))
    {
        return option->name;
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << code;
    return oss.str();
}

} // namespace controller
