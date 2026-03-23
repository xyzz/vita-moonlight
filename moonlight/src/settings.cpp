/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#include "settings.hpp"

#include <borealis.hpp>
#include <string>

#include "ConfigManager.hpp"
#include "tab/settings_tab.hpp"

using namespace brls;

namespace moonlight
{

void settings::loadSettingsFromConfig()
{
    ConfigManager config;
    config.load();
    std::string lang = config.get("general", "language", "en-US");
    // Adjust visual selector (only after init)
    if (lang == "es" || lang == "es-ES")
    {
        if (SettingsTab::languageSelectorPtr)
            SettingsTab::languageSelectorPtr->setSelection(0);
    }
    else
    {
        if (SettingsTab::languageSelectorPtr)
            SettingsTab::languageSelectorPtr->setSelection(1);
    }
    // Here you can load more settings and apply them
}

void settings::saveSettingsToConfig()
{
    ConfigManager config;
    // Example: save language
    int langIdx = 1;
    if (SettingsTab::languageSelectorPtr)
        langIdx = SettingsTab::languageSelectorPtr->getSelection();
    std::string lang = (langIdx == 0) ? "es" : "en-US";
    config.set("general", "language", lang);
    // Here you can save more settings
    config.save();
}

std::string settings::getLanguageFromConfig()
{
    ConfigManager config;
    if (!config.load())
        return "en-US"; // Default value if unable to load
    return config.get("general", "language", "en-US");
}

void settings::applyLanguageEnv(const std::string& lang)
{
#ifdef _WIN32
    _putenv_s("LANG", lang.c_str());
    _putenv_s("BOREALIS_LANG", lang.c_str());
#else
    setenv("LANG", lang.c_str(), 1);
    setenv("BOREALIS_LANG", lang.c_str(), 1);
#endif
}

} // namespace moonlight
