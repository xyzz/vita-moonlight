/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#include "Settings.hpp"

#include <algorithm>
#include <borealis.hpp>
#include <string>
#include <vector>

#include "ConfigManager.hpp"

using namespace brls;

namespace moonlight
{

namespace
{

    constexpr std::size_t UNSYNCED_FRAME_LIMIT = 61;

}

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

void settings::applySwapInterval(int swapInterval)
{
    Application::setSwapInterval(swapInterval);
    Application::setLimitedFPS(swapInterval == 0 ? UNSYNCED_FRAME_LIMIT : 0);
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

void settings::applyBackground(brls::View* view, float opacity)
{
    if (!view)
    {
        return;
    }

    NVGcolor background = Application::getTheme().getColor("brls/background");
    background.a        = std::max(0.0f, std::min(1.0f, opacity));

    AppletFrame* frame = nullptr;
    for (View* parent = view->getParent(); parent != nullptr; parent = parent->getParent())
    {
        frame = dynamic_cast<AppletFrame*>(parent);
        if (frame)
        {
            break;
        }
    }

    if (frame)
    {
        view->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        frame->setBackgroundColor(background);
    }
    else
    {
        view->setBackgroundColor(background);
    }
}

void settings::pushView(brls::View* view, const std::string& title)
{
    if (!view)
    {
        return;
    }

    auto* frame = new AppletFrame(view);
    ConfigManager config;
    config.load();
    applyBackground(view, config.getVideoSettings().settings_background_opacity);
    frame->setTitle(title);
    Application::pushActivity(new Activity(frame), TransitionAnimation::FADE);
}

} // namespace moonlight
