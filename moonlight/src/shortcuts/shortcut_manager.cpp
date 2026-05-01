#include "shortcuts/shortcut_manager.hpp"

#include <psp2/kernel/processmgr.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "shortcuts/shortcut_actions.hpp"

namespace shortcuts
{
namespace
{

    std::uint32_t comboToMask(const ShortcutCombo& combo)
    {
        std::uint32_t mask = 0;
        for (std::uint32_t button : combo.buttons)
        {
            mask |= button;
        }
        return mask;
    }

    ShortcutCombo normalizeForStorage(const ShortcutCombo& combo)
    {
        ShortcutCombo normalized = combo;
        normalizeCombo(normalized);
        return normalized;
    }

    std::string trimWhitespace(std::string value)
    {
        auto notSpace = [](unsigned char c)
        { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    std::string normalizeNameKey(const std::string& value)
    {
        std::string normalized = trimWhitespace(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c)
            { return static_cast<char>(std::tolower(c)); });
        return normalized;
    }

    bool containsNameKey(const std::vector<ShortcutEntry>& entries, const std::string& key, std::size_t ignoreIndex)
    {
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (i == ignoreIndex)
            {
                continue;
            }
            if (normalizeNameKey(entries[i].name) == key)
            {
                return true;
            }
        }
        return false;
    }

    std::string generateDefaultCustomName(const std::vector<ShortcutEntry>& entries, std::size_t ignoreIndex)
    {
        for (std::size_t index = 1; index < 10000; ++index)
        {
            const std::string candidate = "Shortcut " + std::to_string(index);
            if (!containsNameKey(entries, normalizeNameKey(candidate), ignoreIndex))
            {
                return candidate;
            }
        }
        return "Shortcut";
    }

    void ensureCustomName(ShortcutEntry& entry, const std::vector<ShortcutEntry>& entries, std::size_t ignoreIndex)
    {
        entry.name = trimWhitespace(entry.name);
        if (entry.name.empty())
        {
            entry.name = generateDefaultCustomName(entries, ignoreIndex);
        }
    }

} // namespace

ShortcutManager& ShortcutManager::instance()
{
    static ShortcutManager manager;
    return manager;
}

ShortcutManager::ShortcutManager()
{
    reloadConfig();
}

void ShortcutManager::reloadConfig()
{
    std::lock_guard<std::mutex> lock(mutex);
    store.load(config);
    rebuildRuntimeEntries();
}

ShortcutConfig ShortcutManager::getConfig() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return config;
}

std::vector<ShortcutEntry> ShortcutManager::getShortcutEntries() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return runtimeEntries;
}

bool ShortcutManager::processPhysicalInput(const SceCtrlData* pad, const SceCtrlData* /*padOld*/)
{
    if (!pad)
    {
        return false;
    }

    const std::uint64_t nowUs          = sceKernelGetSystemTimeWide();
    const std::uint32_t pressedButtons = pad->buttons;

    ShortcutAction actionToExecute = ShortcutAction::Pause;
    bool shouldExecute             = false;

    {
        std::lock_guard<std::mutex> lock(mutex);
        if (lastExecutionUs != 0 && (nowUs - lastExecutionUs) < 250000)
        {
            return false;
        }

        for (std::size_t i = 0; i < runtimeEntries.size(); ++i)
        {
            if (processAction(runtimeEntries[i].combo, pressedButtons, nowUs, runtimeState[i]))
            {
                actionToExecute = runtimeEntries[i].action;
                lastExecutionUs = nowUs;
                shouldExecute   = true;
                break;
            }
        }
    }

    if (!shouldExecute)
    {
        return false;
    }

    return executeShortcutAction(actionToExecute);
}

bool ShortcutManager::triggerVirtualShortcut(std::uint32_t specialKey)
{
    return executeVirtualShortcut(specialKey);
}

bool ShortcutManager::setActionCombo(ShortcutAction action, const ShortcutCombo& combo, bool persist)
{
    if (!isTrackedAction(action))
    {
        return false;
    }

    ShortcutConfig configToPersist;
    {
        std::lock_guard<std::mutex> lock(mutex);

        const ShortcutCombo normalized = normalizeForStorage(combo);
        if (action == ShortcutAction::Pause)
        {
            config.pause = normalized;
        }
        else if (action == ShortcutAction::Keyboard)
        {
            config.keyboard = normalized;
        }
        else
        {
            return false;
        }

        rebuildRuntimeEntries();
        configToPersist = config;
    }

    if (!persist)
    {
        return true;
    }
    return store.save(configToPersist);
}

bool ShortcutManager::setShortcutCombo(std::size_t index, const ShortcutCombo& combo, bool persist)
{
    ShortcutConfig configToPersist;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (index >= runtimeEntries.size())
        {
            return false;
        }

        const ShortcutCombo normalized = normalizeForStorage(combo);
        if (index == 0)
        {
            config.pause = normalized;
        }
        else if (index == 1)
        {
            config.keyboard = normalized;
        }
        else
        {
            const std::size_t customIndex = index - 2;
            if (customIndex >= config.customShortcuts.size())
            {
                return false;
            }
            config.customShortcuts[customIndex].combo = normalized;
        }

        rebuildRuntimeEntries();
        configToPersist = config;
    }

    if (!persist)
    {
        return true;
    }
    return store.save(configToPersist);
}

bool ShortcutManager::setShortcut(std::size_t index, const ShortcutEntry& entry, bool persist)
{
    if (index < 2)
    {
        return setShortcutCombo(index, entry.combo, persist);
    }

    if (!isTrackedAction(entry.action))
    {
        return false;
    }

    ShortcutConfig configToPersist;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (index >= runtimeEntries.size())
        {
            return false;
        }

        const std::size_t customIndex = index - 2;
        if (customIndex >= config.customShortcuts.size())
        {
            return false;
        }

        ShortcutEntry updated = entry;
        updated.combo         = normalizeForStorage(updated.combo);
        ensureCustomName(updated, config.customShortcuts, customIndex);
        config.customShortcuts[customIndex] = updated;

        rebuildRuntimeEntries();
        configToPersist = config;
    }

    if (!persist)
    {
        return true;
    }
    return store.save(configToPersist);
}

bool ShortcutManager::addShortcut(const ShortcutEntry& entry, bool persist)
{
    if (!isTrackedAction(entry.action))
    {
        return false;
    }

    ShortcutConfig configToPersist;
    {
        std::lock_guard<std::mutex> lock(mutex);
        ShortcutEntry toAdd = entry;
        toAdd.combo         = normalizeForStorage(toAdd.combo);
        ensureCustomName(toAdd, config.customShortcuts, static_cast<std::size_t>(-1));
        config.customShortcuts.push_back(toAdd);

        rebuildRuntimeEntries();
        configToPersist = config;
    }

    if (!persist)
    {
        return true;
    }
    return store.save(configToPersist);
}

bool ShortcutManager::removeShortcut(std::size_t index, bool persist)
{
    ShortcutConfig configToPersist;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (index < 2 || index >= runtimeEntries.size())
        {
            return false;
        }

        const std::size_t customIndex = index - 2;
        if (customIndex >= config.customShortcuts.size())
        {
            return false;
        }
        config.customShortcuts.erase(config.customShortcuts.begin() + static_cast<std::ptrdiff_t>(customIndex));

        rebuildRuntimeEntries();
        configToPersist = config;
    }

    if (!persist)
    {
        return true;
    }
    return store.save(configToPersist);
}

bool ShortcutManager::restoreDefaults(bool persist)
{
    ShortcutConfig configToPersist;
    {
        std::lock_guard<std::mutex> lock(mutex);
        config = defaultShortcutConfig();
        rebuildRuntimeEntries();
        configToPersist = config;
    }

    if (!persist)
    {
        return true;
    }
    return store.save(configToPersist);
}

bool ShortcutManager::processAction(const ShortcutCombo& combo, std::uint32_t pressedButtons, std::uint64_t nowUs, ActionRuntimeState& state)
{
    const std::uint32_t requiredMask = comboToMask(combo);
    if (requiredMask == 0)
    {
        state = ActionRuntimeState {};
        return false;
    }

    const std::uint32_t currentlyPressedRequired = pressedButtons & requiredMask;

    if (state.blockedUntilRelease)
    {
        if (currentlyPressedRequired == 0)
        {
            state.blockedUntilRelease = false;
            state.seenMask            = 0;
            state.tracking            = false;
        }
        return false;
    }

    if (!state.tracking)
    {
        if (currentlyPressedRequired != 0)
        {
            state.tracking        = true;
            state.trackingStartUs = nowUs;
            state.seenMask        = currentlyPressedRequired;
        }
        return false;
    }

    if ((nowUs - state.trackingStartUs) > 500000)
    {
        state = ActionRuntimeState {};
        return false;
    }

    state.seenMask |= currentlyPressedRequired;

    if (currentlyPressedRequired == 0)
    {
        state = ActionRuntimeState {};
        return false;
    }

    if (state.seenMask == requiredMask && currentlyPressedRequired == requiredMask)
    {
        state.tracking            = false;
        state.blockedUntilRelease = true;
        state.seenMask            = 0;
        return true;
    }

    return false;
}

void ShortcutManager::rebuildRuntimeEntries()
{
    runtimeEntries.clear();
    runtimeEntries.push_back({ ShortcutAction::Pause, normalizeForStorage(config.pause), "" });
    runtimeEntries.push_back({ ShortcutAction::Keyboard, normalizeForStorage(config.keyboard), "" });

    for (const auto& entry : config.customShortcuts)
    {
        if (!isTrackedAction(entry.action))
        {
            continue;
        }
        runtimeEntries.push_back({ entry.action, normalizeForStorage(entry.combo), trimWhitespace(entry.name) });
    }

    runtimeState.assign(runtimeEntries.size(), ActionRuntimeState {});
}

bool ShortcutManager::isTrackedAction(ShortcutAction action)
{
    return action == ShortcutAction::Pause || action == ShortcutAction::Keyboard;
}

} // namespace shortcuts
