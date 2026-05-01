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
    BRLS_BIND(brls::SelectorCell, keyboardLayoutSelector, "keyboard_layout_selector");
    BRLS_BIND(brls::DetailCell, themePreviewCell, "theme_preview");
    BRLS_BIND(brls::DetailCell, themeEditorCell, "theme_editor");

    // Lifetime management
    std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);
};
