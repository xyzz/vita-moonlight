/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <string>

namespace moonlight
{

class settings
{
  public:
    static void loadSettingsFromConfig();
    static void saveSettingsToConfig();
    static std::string getLanguageFromConfig();
    static void applyLanguageEnv(const std::string& lang);
};

} // namespace moonlight
