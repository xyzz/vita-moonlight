#pragma once

#include <cstdint>
#include <functional>

#include "shortcuts/shortcut_config.hpp"

namespace shortcuts
{

using ShortcutCallback = std::function<void()>;

void setPauseShortcutCallback(const ShortcutCallback& cb);
void setKeyboardShortcutCallback(const ShortcutCallback& cb);

bool executeShortcutAction(ShortcutAction action);
bool executeVirtualShortcut(std::uint32_t specialKey);

} // namespace shortcuts
