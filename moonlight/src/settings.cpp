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
    // Ajustar el selector visual (solo después de init)
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
    // Aquí puedes cargar más settings y aplicarlos
}

void settings::saveSettingsToConfig()
{
    ConfigManager config;
    // Ejemplo: guardar el idioma
    int langIdx = 1;
    if (SettingsTab::languageSelectorPtr)
        langIdx = SettingsTab::languageSelectorPtr->getSelection();
    std::string lang = (langIdx == 0) ? "es" : "en-US";
    config.set("general", "language", lang);
    // Aquí puedes guardar más settings
    config.save();
}

std::string settings::getLanguageFromConfig()
{
    ConfigManager config;
    if (!config.load())
        return "en-US"; // Valor por defecto si no se puede cargar
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
