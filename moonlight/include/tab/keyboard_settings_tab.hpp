#pragma once

#include <borealis.hpp>
#include <memory>

class KeyboardSettingsTab : public brls::Box
{
  public:
    KeyboardSettingsTab();
    ~KeyboardSettingsTab() override = default;

    static brls::View* create();

  private:
    // UI Elements
    BRLS_BIND(brls::SelectorCell, keyboardModeSelector, "keyboard_mode_selector");
    BRLS_BIND(brls::Header, keyboardLayoutHeader, "keyboard_layout_header");
    BRLS_BIND(brls::SelectorCell, keyboardLayoutSelector, "keyboard_layout_selector");
    BRLS_BIND(brls::BooleanCell, keyboardInputModeToggle, "keyboard_input_mode_toggle");
    BRLS_BIND(brls::Header, keyboardModernHeader, "keyboard_modern_header");
    BRLS_BIND(brls::BooleanCell, keyboardNumbersToggle, "keyboard_numbers_toggle");
    BRLS_BIND(brls::BooleanCell, keyboardArrowsToggle, "keyboard_arrows_toggle");
    BRLS_BIND(brls::Header, keyboardThemeHeader, "keyboard_theme_header");
    BRLS_BIND(brls::DetailCell, themePreviewCell, "theme_preview");
    BRLS_BIND(brls::DetailCell, themeEditorCell, "theme_editor");
    BRLS_BIND(brls::Label, keyboardThemeInfo, "keyboard_theme_info");

    // Lifetime management
    std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);
};
