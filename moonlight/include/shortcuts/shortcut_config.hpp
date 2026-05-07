#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace shortcuts
{

enum class ShortcutAction : std::uint32_t
{
    Pause    = 0,
    Keyboard = 1,
};

inline constexpr std::size_t MAX_SHORTCUT_BUTTONS = 3;

struct ShortcutCombo
{
    std::array<std::uint32_t, MAX_SHORTCUT_BUTTONS> buttons {};
};

struct ShortcutEntry
{
    ShortcutAction action = ShortcutAction::Keyboard;
    ShortcutCombo combo {};
    std::string name;
};

struct ShortcutConfig
{
    ShortcutCombo pause;
    ShortcutCombo keyboard;
    std::vector<ShortcutEntry> customShortcuts;
    int schemaVersion = 3;
};

struct ButtonOption
{
    std::uint32_t mask;
    const char* token;
    const char* label;
};

const std::vector<ButtonOption>& getButtonOptions();
std::size_t getButtonOptionIndex(std::uint32_t mask);
std::uint32_t getButtonMaskForIndex(std::size_t index);

bool comboHasAnyButton(const ShortcutCombo& combo);
bool normalizeCombo(ShortcutCombo& combo);
bool parseCombo(const std::string& text, ShortcutCombo& outCombo);
std::string comboToString(const ShortcutCombo& combo);
std::string comboToDisplay(const ShortcutCombo& combo);
const char* shortcutActionToToken(ShortcutAction action);
bool parseShortcutAction(const std::string& text, ShortcutAction& outAction);

ShortcutConfig defaultShortcutConfig();

class ShortcutConfigStore
{
  public:
    std::string getConfigPath() const;
    bool load(ShortcutConfig& outConfig) const;
    bool save(const ShortcutConfig& config) const;
};

} // namespace shortcuts
