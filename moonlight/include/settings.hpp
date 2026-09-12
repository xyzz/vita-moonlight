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

enum class StreamDisplayMode
{
    Immediate = 0,
    VBlank,
    NextFrame,
};

class settings
{
  public:
    static void applyMenuDisplayMode();
    static const std::vector<StreamDisplayMode>& streamDisplayModes(int renderMode);
    static StreamDisplayMode normalizeStreamDisplayMode(int displayMode, int renderMode);
    static void applyStreamDisplayMode(int displayMode, int renderMode, bool enableFramePacer, int targetFps);
    static std::string getLanguageFromConfig();
    static void applyLanguageEnv(const std::string& lang);
    static const std::vector<LanguageOption>& supportedLanguages();
    static std::string normalizeLanguage(const std::string& lang);
    static std::size_t languageIndex(const std::string& lang);
    static void applyBackground(brls::View* view, float opacity);
    static void pushView(brls::View* view, const std::string& title);
};

} // namespace moonlight
