/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace brls
{
class View;
}

namespace moonlight
{

struct LanguageOption
{
    const char* locale;
    const char* label;
};

class settings
{
  public:
    static void applySwapInterval(int swapInterval);
    static std::string getLanguageFromConfig();
    static void applyLanguageEnv(const std::string& lang);
    static const std::vector<LanguageOption>& supportedLanguages();
    static std::string normalizeLanguage(const std::string& lang);
    static std::size_t languageIndex(const std::string& lang);
    static void applyBackground(brls::View* view, float opacity);
    static void pushView(brls::View* view, const std::string& title);
};

} // namespace moonlight
