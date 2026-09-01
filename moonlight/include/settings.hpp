/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <cstddef>
#include <string>
#include <vector>

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
    static void loadSettingsFromConfig();
    static void saveSettingsToConfig();
    static std::string getLanguageFromConfig();
    static void applyLanguageEnv(const std::string& lang);
    static const std::vector<LanguageOption>& supportedLanguages();
    static std::string normalizeLanguage(const std::string& lang);
    static std::size_t languageIndex(const std::string& lang);
};

} // namespace moonlight
