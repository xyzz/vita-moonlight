#include "shortcuts/shortcut_actions.hpp"

#include "controller/special_inputs.hpp"
#include "debug.hpp"

namespace shortcuts
{
namespace
{

    ShortcutCallback g_pauseShortcutCallback;
    ShortcutCallback g_keyboardShortcutCallback;

} // namespace

void setPauseShortcutCallback(const ShortcutCallback& cb)
{
    g_pauseShortcutCallback = cb;
}

void setKeyboardShortcutCallback(const ShortcutCallback& cb)
{
    g_keyboardShortcutCallback = cb;
}

bool executeShortcutAction(ShortcutAction action)
{
    switch (action)
    {
        case ShortcutAction::Pause:
            if (g_pauseShortcutCallback)
            {
                g_pauseShortcutCallback();
                return true;
            }
            break;
        case ShortcutAction::Keyboard:
            if (g_keyboardShortcutCallback)
            {
                g_keyboardShortcutCallback();
                return true;
            }
            break;
        default:
            break;
    }

    vita_log::info("[Shortcuts] Action requested without callback: %u", static_cast<unsigned>(action));
    return false;
}

bool executeVirtualShortcut(std::uint32_t specialKey)
{
    switch (specialKey)
    {
        case controller::INPUT_SPECIAL_KEY_PAUSE:
            return executeShortcutAction(ShortcutAction::Pause);
        case controller::INPUT_SPECIAL_KEY_KEYBOARD:
            return executeShortcutAction(ShortcutAction::Keyboard);
        default:
            return false;
    }
}

} // namespace shortcuts
