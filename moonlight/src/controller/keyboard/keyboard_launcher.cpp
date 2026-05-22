#include "controller/keyboard/keyboard_launcher.hpp"

#include <borealis.hpp>
#include <string>

#include "ConfigManager.hpp"
#include "controller/ControllerInput.hpp"
#include "controller/keyboard/keyboard.hpp"
#include "controller/keyboard/legacy_keyboard.hpp"
#include "debug.hpp"

namespace
{

std::string buildKeyboardCssPath()
{
    std::string cfgPath = ConfigManager::getConfigPath();
    size_t p            = cfgPath.find_last_of("/\\");
    std::string cfgDir  = (p != std::string::npos) ? cfgPath.substr(0, p) : std::string(".");
    return cfgDir + "/keyboard/style.css";
}

} // namespace

bool open_configured_keyboard()
{
    if (!g_controllerInput)
    {
        vita_log::error("[KeyboardLauncher] g_controllerInput is null");
        return false;
    }

    IKeyboard* active = g_controllerInput->getActiveKeyboard();
    if (active)
    {
        vita_log::info("[KeyboardLauncher] Keyboard is already active (%p)", active);
        return false;
    }

    ConfigManager kbConfig;
    kbConfig.load();
    VideoSettings kbSettings = kbConfig.getVideoSettings();

    if (kbSettings.keyboard_mode == 0)
    {
        auto* legacyKb = new LegacyKeyboard();
        legacyKb->open();
        if (!legacyKb->isOpen())
        {
            delete legacyKb;
            vita_log::error("[KeyboardLauncher] Legacy keyboard failed to open");
            return false;
        }
        g_controllerInput->setActiveKeyboard(legacyKb);
        return true;
    }

    auto* keyboard = new KeyboardOverlay(buildKeyboardCssPath());
    auto* activity = new brls::Activity(keyboard);
    brls::Application::pushActivity(activity);
    brls::Application::giveFocus(keyboard->getDefaultFocus());
    keyboard->open();
    return true;
}
