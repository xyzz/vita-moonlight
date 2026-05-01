#include "tab/keyboard_settings_tab.hpp"

#include <algorithm>

#include "ConfigManager.hpp"

KeyboardSettingsTab::KeyboardSettingsTab()
{
    // Inflate from XML
    this->inflateFromXMLRes("xml/tabs/keyboard_settings.xml");

    ConfigManager config;
    config.load();
    VideoSettings videoSettings = config.getVideoSettings();

    // Keyboard Mode Selector (Legacy / Modern)
    std::vector<std::string> keyboardModes = {
        brls::getStr("moonlight/keyboard/mode_legacy"),
        brls::getStr("moonlight/keyboard/mode_modern")
    };
    keyboardModeSelector->init(
        brls::getStr("moonlight/keyboard/mode_title"),
        keyboardModes,
        videoSettings.keyboard_mode,
        [this](int selected)
        {
            ConfigManager cfg;
            cfg.load();
            VideoSettings settings = cfg.getVideoSettings();
            settings.keyboard_mode = selected;
            cfg.setVideoSettings(settings);
            cfg.save();
            brls::Application::notify(
                brls::getStr("moonlight/keyboard/mode_saved"));
        });

    // Keyboard Layout Selector
    std::vector<std::string> keyboardLayouts = { "EN-US", "ES-ES", "ES-LATAM" };
    keyboardLayoutSelector->init(
        brls::getStr("moonlight/settings_tab/keyboard_layout/title"),
        keyboardLayouts,
        videoSettings.keyboard_layout,
        [this](int selected)
        {
            ConfigManager cfg;
            cfg.load();
            VideoSettings settings   = cfg.getVideoSettings();
            settings.keyboard_layout = selected;
            cfg.setVideoSettings(settings);
            cfg.save();
            brls::Application::notify(
                brls::getStr("moonlight/settings_tab/keyboard_layout/saved"));
        });

    // Theme Preview (placeholder - implementation future)
    themePreviewCell->setDetailText("Default");
    themePreviewCell->registerClickAction([](brls::View* view)
        {
        brls::Application::notify("Theme selection coming in future update");
        return true; });

    // Theme Editor (placeholder - implementation future)
    themeEditorCell->setDetailText("Not available");
    themeEditorCell->registerClickAction([](brls::View* view)
        {
        brls::Application::notify("Theme editor coming in future update");
        return true; });
}

brls::View* KeyboardSettingsTab::create()
{
    return new KeyboardSettingsTab();
}
