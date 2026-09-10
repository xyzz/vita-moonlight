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
        if (keyboardInputModeToggle)
        {
            keyboardInputModeToggle->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardLegacyDialogToggle)
        {
            keyboardLegacyDialogToggle->setVisibility(modernMode ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
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
        if (keyboardFullscreenToggle)
        {
            keyboardFullscreenToggle->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardPreviewToggle)
        {
            keyboardPreviewToggle->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
        if (keyboardMouseToggle)
        {
            keyboardMouseToggle->setVisibility(modernMode ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
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
            cfg.updateVideoSettings([selected](VideoSettings& settings)
                { settings.keyboard_mode = selected; });
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
            cfg.updateVideoSettings([selected](VideoSettings& settings)
                { settings.keyboard_layout = selected; });
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
            cfg.updateVideoSettings([value](VideoSettings& settings)
                { settings.keyboard_input_mode = value; });
            brls::Application::notify(value ? brls::getStr("moonlight/keyboard/input_mode_utf8") : brls::getStr("moonlight/keyboard/input_mode_vk"));
        });

    // Modern Keyboard Options
    keyboardNumbersToggle->init(
        brls::getStr("moonlight/keyboard/numbers_row_title"),
        videoSettings.keyboard_numbers_row,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.updateVideoSettings([value](VideoSettings& settings)
                { settings.keyboard_numbers_row = value; });
        });

    keyboardArrowsToggle->init(
        brls::getStr("moonlight/keyboard/arrows_title"),
        videoSettings.keyboard_show_arrows,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.updateVideoSettings([value](VideoSettings& settings)
                { settings.keyboard_show_arrows = value; });
        });

    keyboardLegacyDialogToggle->init(
        brls::getStr("moonlight/keyboard/legacy_dialog_title"),
        videoSettings.keyboard_legacy_dialog,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.updateVideoSettings([value](VideoSettings& settings)
                { settings.keyboard_legacy_dialog = value; });
        });

    keyboardFullscreenToggle->init(
        brls::getStr("moonlight/keyboard/fullscreen_title"),
        videoSettings.keyboard_fullscreen,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.updateVideoSettings([value](VideoSettings& settings)
                { settings.keyboard_fullscreen = value; });
        });

    keyboardPreviewToggle->init(
        brls::getStr("moonlight/keyboard/text_preview_title"),
        videoSettings.keyboard_text_preview,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.updateVideoSettings([value](VideoSettings& settings)
                { settings.keyboard_text_preview = value; });
        });

    keyboardMouseToggle->init(
        brls::getStr("moonlight/keyboard/mouse_passthrough_title"),
        videoSettings.keyboard_mouse_passthrough,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.updateVideoSettings([value](VideoSettings& settings)
                { settings.keyboard_mouse_passthrough = value; });
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
