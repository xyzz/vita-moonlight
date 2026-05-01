#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "shortcuts/shortcut_config.hpp"

class ShortcutsSettingsTab : public brls::Box
{
  public:
    ShortcutsSettingsTab();

    static brls::View* create();

  private:
    void rebuildShortcutList();
    void openShortcutEditor(std::size_t index, bool creatingNew);

    std::vector<std::string> buttonLabels;
    std::vector<std::string> actionLabels;

    BRLS_BIND(brls::Box, shortcutsListContainer, "shortcutsListContainer");
};
