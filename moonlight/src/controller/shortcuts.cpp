#include "controller/shortcuts.hpp"

#include "shortcuts/shortcut_actions.hpp"
#include "shortcuts/shortcut_manager.hpp"

void set_pause_callback(const std::function<void()>& cb)
{
    shortcuts::setPauseShortcutCallback(cb);
}

void set_keyboard_callback(const std::function<void()>& cb)
{
    shortcuts::setKeyboardShortcutCallback(cb);
}

void reload_shortcuts_config()
{
    shortcuts::ShortcutManager::instance().reloadConfig();
}

bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old)
{
    return shortcuts::ShortcutManager::instance().processPhysicalInput(pad, pad_old);
}

bool trigger_virtual_shortcut(std::uint32_t specialKey)
{
    return shortcuts::ShortcutManager::instance().triggerVirtualShortcut(specialKey);
}
