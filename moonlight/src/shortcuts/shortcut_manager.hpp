#pragma once

#include <psp2/ctrl.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "shortcuts/shortcut_config.hpp"

namespace shortcuts
{

class ShortcutManager
{
  public:
    static ShortcutManager& instance();

    bool processPhysicalInput(const SceCtrlData* pad, const SceCtrlData* padOld);
    bool triggerVirtualShortcut(std::uint32_t specialKey);

    void reloadConfig();
    ShortcutConfig getConfig() const;
    std::vector<ShortcutEntry> getShortcutEntries() const;

    bool setActionCombo(ShortcutAction action, const ShortcutCombo& combo, bool persist);
    bool setShortcutCombo(std::size_t index, const ShortcutCombo& combo, bool persist);
    bool setShortcut(std::size_t index, const ShortcutEntry& entry, bool persist);
    bool addShortcut(const ShortcutEntry& entry, bool persist);
    bool removeShortcut(std::size_t index, bool persist);
    bool restoreDefaults(bool persist);

  private:
    ShortcutManager();

    struct ActionRuntimeState
    {
        bool tracking                 = false;
        bool blockedUntilRelease      = false;
        std::uint32_t seenMask        = 0;
        std::uint64_t trackingStartUs = 0;
    };

    bool processAction(const ShortcutCombo& combo, std::uint32_t pressedButtons, std::uint64_t nowUs, ActionRuntimeState& state);
    void rebuildRuntimeEntries();
    static bool isTrackedAction(ShortcutAction action);

    ShortcutConfigStore store;
    ShortcutConfig config;
    std::vector<ShortcutEntry> runtimeEntries;
    std::vector<ActionRuntimeState> runtimeState;
    std::uint64_t lastExecutionUs = 0;
    mutable std::mutex mutex;
};

} // namespace shortcuts
