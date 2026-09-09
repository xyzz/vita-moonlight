#include "shortcuts/shortcut_text.hpp"

#include <algorithm>
#include <cctype>

namespace shortcuts
{

std::string trimWhitespace(std::string value)
{
    auto notSpace = [](unsigned char character)
    { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string normalizeNameKey(const std::string& value)
{
    std::string normalized = trimWhitespace(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character)
        { return static_cast<char>(std::tolower(character)); });
    return normalized;
}

}
