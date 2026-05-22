#include "shortcuts/shortcut_config.hpp"

#include <ini.h>
#include <psp2/ctrl.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

#include "ConfigManager.hpp"
#include "debug.hpp"

namespace shortcuts
{
namespace
{

    const std::vector<ButtonOption> kButtonOptions = {
        { 0, "NONE", "None" },
        { SCE_CTRL_START, "START", "START" },
        { SCE_CTRL_SELECT, "SELECT", "SELECT" },
        { SCE_CTRL_UP, "UP", "DPAD UP" },
        { SCE_CTRL_DOWN, "DOWN", "DPAD DOWN" },
        { SCE_CTRL_LEFT, "LEFT", "DPAD LEFT" },
        { SCE_CTRL_RIGHT, "RIGHT", "DPAD RIGHT" },
        { SCE_CTRL_L1, "L1", "L1" },
        { SCE_CTRL_R1, "R1", "R1" },
        { SCE_CTRL_L2, "L2", "L2" },
        { SCE_CTRL_R2, "R2", "R2" },
        { SCE_CTRL_L3, "L3", "L3" },
        { SCE_CTRL_R3, "R3", "R3" },
        { SCE_CTRL_TRIANGLE, "TRIANGLE", "TRIANGLE" },
        { SCE_CTRL_CIRCLE, "CIRCLE", "CIRCLE" },
        { SCE_CTRL_CROSS, "CROSS", "CROSS" },
        { SCE_CTRL_SQUARE, "SQUARE", "SQUARE" },
    };

    const char* actionTokenInternal(ShortcutAction action)
    {
        switch (action)
        {
            case ShortcutAction::Pause:
                return "PAUSE";
            case ShortcutAction::Keyboard:
                return "KEYBOARD";
            default:
                return "KEYBOARD";
        }
    }

    std::string trimAndUpper(std::string value)
    {
        auto notSpace = [](unsigned char c)
        { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            { return static_cast<char>(std::toupper(c)); });
        return value;
    }

    std::string trimWhitespace(std::string value)
    {
        auto notSpace = [](unsigned char c)
        { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    const ButtonOption* findOptionByToken(const std::string& token)
    {
        for (const auto& option : kButtonOptions)
        {
            if (token == option.token)
            {
                return &option;
            }
        }
        return nullptr;
    }

    bool parseShortcutActionInternal(const std::string& text, ShortcutAction& outAction)
    {
        const std::string token = trimAndUpper(text);
        if (token == "PAUSE")
        {
            outAction = ShortcutAction::Pause;
            return true;
        }
        if (token == "KEYBOARD")
        {
            outAction = ShortcutAction::Keyboard;
            return true;
        }
        return false;
    }

    enum class CustomField
    {
        Action,
        Combo,
        Name,
    };

    bool parseCustomKey(const std::string& key, std::size_t& outIndex, CustomField& outField)
    {
        static constexpr const char* prefix = "custom_";
        if (key.rfind(prefix, 0) != 0)
        {
            return false;
        }

        const std::size_t indexStart   = 7;
        const std::size_t separatorPos = key.find('_', indexStart);
        if (separatorPos == std::string::npos || separatorPos <= indexStart)
        {
            return false;
        }

        const std::string indexToken = key.substr(indexStart, separatorPos - indexStart);
        if (!std::all_of(indexToken.begin(), indexToken.end(), [](unsigned char c)
                { return std::isdigit(c) != 0; }))
        {
            return false;
        }

        const std::string field = key.substr(separatorPos + 1);
        if (field == "action")
        {
            outField = CustomField::Action;
        }
        else if (field == "combo")
        {
            outField = CustomField::Combo;
        }
        else if (field == "name")
        {
            outField = CustomField::Name;
        }
        else
        {
            return false;
        }

        try
        {
            outIndex = static_cast<std::size_t>(std::stoul(indexToken));
        }
        catch (...)
        {
            return false;
        }

        return true;
    }

    ShortcutAction sanitizeAction(ShortcutAction action)
    {
        if (action == ShortcutAction::Pause || action == ShortcutAction::Keyboard)
        {
            return action;
        }
        return ShortcutAction::Keyboard;
    }

    std::string joinCombo(const ShortcutCombo& combo, bool useLabel)
    {
        std::string out;
        for (std::uint32_t mask : combo.buttons)
        {
            if (mask == 0)
            {
                continue;
            }

            const auto idx = getButtonOptionIndex(mask);
            if (idx >= kButtonOptions.size())
            {
                continue;
            }

            const char* part = useLabel ? kButtonOptions[idx].label : kButtonOptions[idx].token;
            if (part == nullptr || part[0] == '\0')
            {
                continue;
            }

            if (!out.empty())
            {
                out += useLabel ? " + " : "+";
            }
            out += part;
        }

        if (out.empty())
        {
            return useLabel ? "None" : "NONE";
        }

        return out;
    }

    int iniHandler(void* user, const char* section, const char* name, const char* value)
    {
        if (!user || !section || !name || !value)
        {
            return 1;
        }

        auto* config = reinterpret_cast<ShortcutConfig*>(user);
        if (std::string(section) != "shortcuts")
        {
            return 1;
        }

        const std::string key(name);
        if (key == "pause")
        {
            ShortcutCombo combo {};
            if (parseCombo(value, combo))
            {
                config->pause = combo;
            }
        }
        else if (key == "keyboard")
        {
            ShortcutCombo combo {};
            if (parseCombo(value, combo))
            {
                config->keyboard = combo;
            }
        }
        else if (key == "schema_version")
        {
            try
            {
                config->schemaVersion = std::stoi(value);
            }
            catch (...)
            {
                config->schemaVersion = 3;
            }
        }
        else if (key == "custom_count")
        {
            try
            {
                config->customShortcuts.resize(static_cast<std::size_t>(std::stoul(value)));
            }
            catch (...)
            {
                config->customShortcuts.clear();
            }
        }
        else
        {
            std::size_t customIndex = 0;
            CustomField field       = CustomField::Combo;
            if (parseCustomKey(key, customIndex, field))
            {
                if (customIndex >= config->customShortcuts.size())
                {
                    config->customShortcuts.resize(customIndex + 1);
                }

                ShortcutEntry& entry = config->customShortcuts[customIndex];
                if (field == CustomField::Action)
                {
                    ShortcutAction action = ShortcutAction::Keyboard;
                    if (parseShortcutAction(value, action))
                    {
                        entry.action = action;
                    }
                }
                else if (field == CustomField::Combo)
                {
                    ShortcutCombo combo {};
                    if (parseCombo(value, combo))
                    {
                        entry.combo = combo;
                    }
                }
                else
                {
                    entry.name = trimWhitespace(value);
                }
            }
        }

        return 1;
    }

} // namespace

const std::vector<ButtonOption>& getButtonOptions()
{
    return kButtonOptions;
}

std::size_t getButtonOptionIndex(std::uint32_t mask)
{
    for (std::size_t i = 0; i < kButtonOptions.size(); ++i)
    {
        if (kButtonOptions[i].mask == mask)
        {
            return i;
        }
    }
    return 0;
}

std::uint32_t getButtonMaskForIndex(std::size_t index)
{
    if (index >= kButtonOptions.size())
    {
        return 0;
    }
    return kButtonOptions[index].mask;
}

bool comboHasAnyButton(const ShortcutCombo& combo)
{
    for (std::uint32_t button : combo.buttons)
    {
        if (button != 0)
        {
            return true;
        }
    }
    return false;
}

bool normalizeCombo(ShortcutCombo& combo)
{
    ShortcutCombo normalized {};
    std::unordered_set<std::uint32_t> seen;

    std::size_t outIndex = 0;
    for (std::uint32_t button : combo.buttons)
    {
        if (button == 0 || seen.count(button) != 0)
        {
            continue;
        }
        if (outIndex >= normalized.buttons.size())
        {
            break;
        }
        normalized.buttons[outIndex++] = button;
        seen.insert(button);
    }

    combo = normalized;
    return outIndex > 0;
}

bool parseCombo(const std::string& text, ShortcutCombo& outCombo)
{
    const std::string normalizedText = trimAndUpper(text);
    if (normalizedText.empty() || normalizedText == "NONE")
    {
        outCombo = ShortcutCombo {};
        return true;
    }

    ShortcutCombo parsed {};

    std::stringstream ss(text);
    std::string segment;
    std::size_t idx = 0;

    while (std::getline(ss, segment, '+'))
    {
        const std::string token = trimAndUpper(segment);
        if (token.empty() || token == "NONE")
        {
            continue;
        }

        const auto* option = findOptionByToken(token);
        if (!option || option->mask == 0)
        {
            continue;
        }

        if (idx < parsed.buttons.size())
        {
            parsed.buttons[idx++] = option->mask;
        }
    }

    if (idx == 0)
    {
        return false;
    }

    normalizeCombo(parsed);

    outCombo = parsed;
    return true;
}

std::string comboToString(const ShortcutCombo& combo)
{
    return joinCombo(combo, false);
}

std::string comboToDisplay(const ShortcutCombo& combo)
{
    return joinCombo(combo, true);
}

const char* shortcutActionToToken(ShortcutAction action)
{
    return actionTokenInternal(action);
}

bool parseShortcutAction(const std::string& text, ShortcutAction& outAction)
{
    return parseShortcutActionInternal(text, outAction);
}

ShortcutConfig defaultShortcutConfig()
{
    ShortcutConfig config;
    config.schemaVersion    = 3;
    config.pause.buttons    = { SCE_CTRL_START, SCE_CTRL_L1, SCE_CTRL_R1 };
    config.keyboard.buttons = { SCE_CTRL_START, SCE_CTRL_LEFT, 0 };
    config.customShortcuts.clear();
    return config;
}

std::string ShortcutConfigStore::getConfigPath() const
{
    std::string configPath = ConfigManager::getConfigPath();
    const auto slashPos    = configPath.find_last_of("/\\");
    if (slashPos == std::string::npos)
    {
        return "shortcuts.conf";
    }
    return configPath.substr(0, slashPos + 1) + "shortcuts.conf";
}

bool ShortcutConfigStore::load(ShortcutConfig& outConfig) const
{
    outConfig = defaultShortcutConfig();

    const std::string path = getConfigPath();
    const int parseResult  = ini_parse(path.c_str(), iniHandler, &outConfig);

    normalizeCombo(outConfig.pause);
    normalizeCombo(outConfig.keyboard);

    for (auto& entry : outConfig.customShortcuts)
    {
        entry.action = sanitizeAction(entry.action);
        normalizeCombo(entry.combo);
        entry.name = trimWhitespace(entry.name);
    }

    if (parseResult != 0)
    {
        vita_log::error("[Shortcuts] shortcuts.conf not found or invalid, writing defaults");
        save(outConfig);
        return false;
    }

    return true;
}

bool ShortcutConfigStore::save(const ShortcutConfig& config) const
{
    ShortcutConfig sanitized = config;
    normalizeCombo(sanitized.pause);
    normalizeCombo(sanitized.keyboard);

    for (auto& entry : sanitized.customShortcuts)
    {
        entry.action = sanitizeAction(entry.action);
        normalizeCombo(entry.combo);
        entry.name = trimWhitespace(entry.name);
    }

    if (sanitized.schemaVersion < 3)
    {
        sanitized.schemaVersion = 3;
    }

    const std::string path = getConfigPath();
    const auto slashPos    = path.find_last_of("/\\");
    if (slashPos != std::string::npos)
    {
        const std::string dirPath = path.substr(0, slashPos);
        ConfigManager cfg;
        cfg.ensureDirExists(dirPath);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
    {
        vita_log::error("[Shortcuts] Failed to open shortcuts config for write: %s", path.c_str());
        return false;
    }

    out << "[shortcuts]\n";
    out << "schema_version=" << sanitized.schemaVersion << "\n";
    out << "pause=" << comboToString(sanitized.pause) << "\n";
    out << "keyboard=" << comboToString(sanitized.keyboard) << "\n";
    out << "custom_count=" << sanitized.customShortcuts.size() << "\n";

    for (std::size_t i = 0; i < sanitized.customShortcuts.size(); ++i)
    {
        const auto& entry = sanitized.customShortcuts[i];
        out << "custom_" << i << "_action=" << shortcutActionToToken(entry.action) << "\n";
        out << "custom_" << i << "_combo=" << comboToString(entry.combo) << "\n";
        out << "custom_" << i << "_name=" << entry.name << "\n";
    }

    return true;
}

} // namespace shortcuts
