/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#include "settings.hpp"

#include <algorithm>
#include <borealis.hpp>
#include <string>
#include <vector>

#include "ConfigManager.hpp"
#include "tab/settings_tab.hpp"

using namespace brls;

namespace moonlight
{

const std::vector<LanguageOption>& settings::supportedLanguages()
{
    static const std::vector<LanguageOption> languages = {
        { "en-US", "English" },
        { "es", "Español" },
        { "fr", "Français" },
        { "pt-BR", "Português (Brasil)" },
        { "ru", "Русский" },
        { "ja", "日本語" },
        { "zh-Hant", "繁體中文" },
    };
    return languages;
}

std::string settings::normalizeLanguage(const std::string& lang)
{
    if (lang == "es" || lang == "es-ES")
        return "es";
    if (lang == "fr" || lang == "fr-FR")
        return "fr";
    if (lang == "pt" || lang == "pt-BR" || lang == "pt-PT")
        return "pt-BR";
    if (lang == "ru" || lang == "ru-RU")
        return "ru";
    if (lang == "ja" || lang == "ja-JP")
        return "ja";
    if (lang == "zh" || lang == "zh-CN" || lang == "zh-SG" || lang == "zh-Hans" || lang == "zh-TW" || lang == "zh-HK" || lang == "zh-Hant")
        return "zh-Hant";
    return "en-US";
}

std::size_t settings::languageIndex(const std::string& lang)
{
    const std::string locale = normalizeLanguage(lang);
    const auto& languages    = supportedLanguages();
    auto found               = std::find_if(languages.begin(), languages.end(), [&locale](const LanguageOption& option)
                      { return locale == option.locale; });
    return found == languages.end() ? 0 : static_cast<std::size_t>(found - languages.begin());
}

void settings::loadSettingsFromConfig()
{
    ConfigManager config;
    config.load();
    if (SettingsTab::languageSelectorPtr)
        SettingsTab::languageSelectorPtr->setSelection(static_cast<int>(languageIndex(config.get("general", "language", "en-US"))));
}

void settings::saveSettingsToConfig()
{
    ConfigManager config;
    config.load();
    int langIdx = 0;
    if (SettingsTab::languageSelectorPtr)
        langIdx = SettingsTab::languageSelectorPtr->getSelection();
    const auto& languages = supportedLanguages();
    if (langIdx < 0 || static_cast<std::size_t>(langIdx) >= languages.size())
        langIdx = 0;
    config.set("general", "language", languages[langIdx].locale);
    config.save();
}

std::string settings::getLanguageFromConfig()
{
    ConfigManager config;
    if (!config.load())
        return "en-US";
    return normalizeLanguage(config.get("general", "language", "en-US"));
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
