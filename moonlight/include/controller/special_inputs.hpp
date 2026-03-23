#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace controller
{

struct SpecialInputOption
{
    std::uint32_t code;
    const char* name;
    bool selectable;
};

inline constexpr std::uint32_t INPUT_SPECIAL_KEY_PAUSE    = 0;
inline constexpr std::uint32_t INPUT_SPECIAL_KEY_KEYBOARD = 1;

const std::vector<SpecialInputOption>& getAllSpecialInputOptions();
const std::vector<SpecialInputOption>& getSelectableSpecialInputOptions();
const SpecialInputOption* findSpecialInputOption(std::uint32_t code);
std::size_t getSelectableIndexForCode(std::uint32_t code);
std::uint32_t getCodeForSelectableIndex(std::size_t index);
std::string getDisplayNameForCode(std::uint32_t code);

} // namespace controller
