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

    auto updateLayoutVisibility = [this](int mode)
    {
        const bool modernMode = (mode != 0);
        if (keyboardLayoutHeader)
        {
            keyboardLayoutHeader->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardLayoutSelector)
        {
            keyboardLayoutSelector->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardModernHeader)
        {
            keyboardModernHeader->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardNumbersToggle)
        {
            keyboardNumbersToggle->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardArrowsToggle)
        {
            keyboardArrowsToggle->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardThemeHeader)
        {
            keyboardThemeHeader->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (themePreviewCell)
        {
            themePreviewCell->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (themeEditorCell)
        {
            themeEditorCell->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardThemeInfo)
        {
            keyboardThemeInfo->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
    };

    // Keyboard Mode Selector (Legacy / Modern)
    std::vector<std::string> keyboardModes = {
        brls::getStr("moonlight/keyboard/mode_legacy"),
        brls::getStr("moonlight/keyboard/mode_modern")
    };
    keyboardModeSelector->init(
        brls::getStr("moonlight/keyboard/mode_title"),
        keyboardModes,
        videoSettings.keyboard_mode,
        [this, updateLayoutVisibility](int selected)
        {
            ConfigManager cfg;
            cfg.load();
            VideoSettings settings = cfg.getVideoSettings();
            settings.keyboard_mode = selected;
            cfg.setVideoSettings(settings);
            cfg.save();
            brls::Application::notify(
                brls::getStr("moonlight/keyboard/mode_saved"));
            updateLayoutVisibility(selected);
        });

    // Keyboard Layout Selector (client on-screen keyboard display)
    std::vector<std::string> keyboardLayouts = { "EN-US", "ES-ES", "ES-MX" };
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

    // Input Mode Toggle (force UTF-8 text input instead of VK codes)
    keyboardInputModeToggle->init(
        brls::getStr("moonlight/keyboard/input_mode_title"),
        videoSettings.keyboard_input_mode,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.load();
            VideoSettings settings       = cfg.getVideoSettings();
            settings.keyboard_input_mode = value;
            cfg.setVideoSettings(settings);
            cfg.save();
            brls::Application::notify(value ? brls::getStr("moonlight/keyboard/input_mode_utf8") : brls::getStr("moonlight/keyboard/input_mode_vk"));
        });

    // Modern Keyboard Options
    keyboardNumbersToggle->init(
        brls::getStr("moonlight/keyboard/numbers_row_title"),
        videoSettings.keyboard_numbers_row,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.load();
            VideoSettings settings        = cfg.getVideoSettings();
            settings.keyboard_numbers_row = value;
            cfg.setVideoSettings(settings);
            cfg.save();
        });

    keyboardArrowsToggle->init(
        brls::getStr("moonlight/keyboard/arrows_title"),
        videoSettings.keyboard_show_arrows,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.load();
            VideoSettings settings        = cfg.getVideoSettings();
            settings.keyboard_show_arrows = value;
            cfg.setVideoSettings(settings);
            cfg.save();
        });

    updateLayoutVisibility(videoSettings.keyboard_mode);

    themePreviewCell->setDetailText(brls::getStr("moonlight/keyboard/theme_default"));
    themePreviewCell->registerClickAction([](brls::View* view)
        {
        brls::Application::notify(brls::getStr("moonlight/keyboard/theme_selection_future"));
        return true; });

    themeEditorCell->setDetailText(brls::getStr("moonlight/keyboard/theme_not_available"));
    themeEditorCell->registerClickAction([](brls::View* view)
        {
        brls::Application::notify(brls::getStr("moonlight/keyboard/theme_editor_future"));
        return true; });
}

brls::View* KeyboardSettingsTab::create()
{
    return new KeyboardSettingsTab();
}
