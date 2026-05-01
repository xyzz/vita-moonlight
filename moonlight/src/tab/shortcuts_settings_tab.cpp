#include "tab/shortcuts_settings_tab.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "controller/shortcuts.hpp"
#include "shortcuts/shortcut_manager.hpp"

namespace
{

std::vector<std::string> buildButtonLabels()
{
    std::vector<std::string> labels;
    const auto& options = shortcuts::getButtonOptions();
    labels.reserve(options.size());
    for (const auto& option : options)
    {
        labels.emplace_back(option.label);
    }
    return labels;
}

int comboButtonToSelection(const shortcuts::ShortcutCombo& combo, std::size_t index)
{
    if (index >= combo.buttons.size())
    {
        return 0;
    }
    return static_cast<int>(shortcuts::getButtonOptionIndex(combo.buttons[index]));
}

void setComboFromSelectors(shortcuts::ShortcutCombo& combo, int first, int second, int third)
{
    combo.buttons = {
        shortcuts::getButtonMaskForIndex(static_cast<std::size_t>(first)),
        shortcuts::getButtonMaskForIndex(static_cast<std::size_t>(second)),
        shortcuts::getButtonMaskForIndex(static_cast<std::size_t>(third)),
    };
    shortcuts::normalizeCombo(combo);
}

int actionToSelection(shortcuts::ShortcutAction action)
{
    return action == shortcuts::ShortcutAction::Pause ? 0 : 1;
}

shortcuts::ShortcutAction selectionToAction(int selection)
{
    return selection == 0 ? shortcuts::ShortcutAction::Pause : shortcuts::ShortcutAction::Keyboard;
}

std::string actionToDisplay(shortcuts::ShortcutAction action)
{
    if (action == shortcuts::ShortcutAction::Pause)
    {
        return brls::getStr("moonlight/shortcuts/action_pause");
    }
    return brls::getStr("moonlight/shortcuts/action_keyboard");
}

std::string comboDisplayOrDisabled(const shortcuts::ShortcutCombo& combo)
{
    if (!shortcuts::comboHasAnyButton(combo))
    {
        return brls::getStr("moonlight/shortcuts/disabled_combo");
    }
    return shortcuts::comboToDisplay(combo);
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
    std::string key = trimWhitespace(value);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c)
        { return static_cast<char>(std::tolower(c)); });
    return key;
}

std::string fallbackCustomTitleFromIndex(std::size_t index)
{
    return brls::getStr("moonlight/shortcuts/custom_shortcut_title") + " " + std::to_string(index - 1);
}

std::string shortcutTitleForEntry(std::size_t index, const shortcuts::ShortcutEntry& entry)
{
    if (index == 0)
    {
        return brls::getStr("moonlight/shortcuts/pause_combo_title");
    }
    if (index == 1)
    {
        return brls::getStr("moonlight/shortcuts/keyboard_combo_title");
    }

    const std::string customName = trimWhitespace(entry.name);
    if (!customName.empty())
    {
        return customName;
    }

    return fallbackCustomTitleFromIndex(index);
}

bool containsCustomName(const std::vector<shortcuts::ShortcutEntry>& entries, const std::string& nameKey, std::size_t ignoreIndex)
{
    for (std::size_t i = 2; i < entries.size(); ++i)
    {
        if (i == ignoreIndex)
        {
            continue;
        }
        if (normalizeNameKey(entries[i].name) == nameKey)
        {
            return true;
        }
    }
    return false;
}

std::string nextDefaultCustomName(const std::vector<shortcuts::ShortcutEntry>& entries, std::size_t ignoreIndex)
{
    for (std::size_t idx = 1; idx < 10000; ++idx)
    {
        const std::string candidate = "Shortcut " + std::to_string(idx);
        if (!containsCustomName(entries, normalizeNameKey(candidate), ignoreIndex))
        {
            return candidate;
        }
    }
    return "Shortcut";
}

} // namespace

ShortcutsSettingsTab::ShortcutsSettingsTab()
{
    this->inflateFromXMLRes("xml/tabs/shortcuts_settings.xml");

    auto& manager = shortcuts::ShortcutManager::instance();
    manager.reloadConfig();

    this->buttonLabels = buildButtonLabels();
    this->actionLabels = {
        brls::getStr("moonlight/shortcuts/action_pause"),
        brls::getStr("moonlight/shortcuts/action_keyboard"),
    };

    rebuildShortcutList();

    this->registerAction(brls::getStr("hints/back"), brls::BUTTON_B, [](brls::View*)
        {
        brls::Application::popActivity();
        return true; });
}

void ShortcutsSettingsTab::rebuildShortcutList()
{
    if (!shortcutsListContainer)
    {
        return;
    }

    shortcutsListContainer->clearViews();

    auto& manager = shortcuts::ShortcutManager::instance();
    auto entries  = manager.getShortcutEntries();

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        auto* cell        = new brls::DetailCell();
        cell->setText(shortcutTitleForEntry(i, entry));
        cell->setDetailText(actionToDisplay(entry.action) + " | " + comboDisplayOrDisabled(entry.combo));

        cell->registerClickAction([this, i](brls::View*)
            {
            openShortcutEditor(i, false);
            return true; });
        shortcutsListContainer->addView(cell);
    }

    auto* addCell = new brls::DetailCell();
    addCell->setText(brls::getStr("moonlight/shortcuts/add_shortcut_title"));
    addCell->setDetailText(brls::getStr("moonlight/shortcuts/add_shortcut_detail"));
    addCell->registerClickAction([this](brls::View*)
        {
        openShortcutEditor(0, true);
        return true; });
    shortcutsListContainer->addView(addCell);
}

void ShortcutsSettingsTab::openShortcutEditor(std::size_t index, bool creatingNew)
{
    auto& manager = shortcuts::ShortcutManager::instance();
    auto entries  = manager.getShortcutEntries();

    if (!creatingNew && index >= entries.size())
    {
        return;
    }

    auto editorState = std::make_shared<shortcuts::ShortcutEntry>();
    if (creatingNew)
    {
        editorState->action = shortcuts::ShortcutAction::Keyboard;
        editorState->combo  = shortcuts::ShortcutCombo {};
    }
    else
    {
        *editorState = entries[index];
    }

    const bool canDelete                  = !creatingNew && index >= 2;
    const bool canChangeAction            = creatingNew || index >= 2;
    const std::size_t ignoreIndex         = creatingNew ? static_cast<std::size_t>(-1) : index;
    const std::string nextNamePlaceholder = nextDefaultCustomName(entries, ignoreIndex);

    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setPadding(20, 40, 20, 40);

    if (canChangeAction)
    {
        auto* actionSelector = new brls::SelectorCell();
        actionSelector->init(
            brls::getStr("moonlight/shortcuts/editor_action_title"),
            actionLabels,
            actionToSelection(editorState->action),
            [editorState](int selected)
            {
                editorState->action = selectionToAction(selected);
            });
        content->addView(actionSelector);
    }
    else
    {
        auto* actionDetail = new brls::DetailCell();
        actionDetail->setText(brls::getStr("moonlight/shortcuts/editor_action_title"));
        actionDetail->setDetailText(actionToDisplay(editorState->action));
        content->addView(actionDetail);
    }

    if (canChangeAction)
    {
        auto* nameInput = new brls::InputCell();
        nameInput->init(
            brls::getStr("moonlight/shortcuts/editor_name_title"),
            trimWhitespace(editorState->name),
            [editorState](std::string text)
            {
                editorState->name = text;
            },
            nextNamePlaceholder,
            brls::getStr("moonlight/shortcuts/editor_name_hint"),
            32);
        content->addView(nameInput);
    }

    auto* button1Selector = new brls::SelectorCell();
    auto* button2Selector = new brls::SelectorCell();
    auto* button3Selector = new brls::SelectorCell();

    auto syncComboFromSelectors = [editorState, button1Selector, button2Selector, button3Selector]()
    {
        setComboFromSelectors(
            editorState->combo,
            button1Selector->getSelection(),
            button2Selector->getSelection(),
            button3Selector->getSelection());
    };

    button1Selector->init(
        brls::getStr("moonlight/shortcuts/button_slot_1"),
        buttonLabels,
        comboButtonToSelection(editorState->combo, 0),
        [syncComboFromSelectors](int)
        {
            syncComboFromSelectors();
        });

    button2Selector->init(
        brls::getStr("moonlight/shortcuts/button_slot_2"),
        buttonLabels,
        comboButtonToSelection(editorState->combo, 1),
        [syncComboFromSelectors](int)
        {
            syncComboFromSelectors();
        });

    button3Selector->init(
        brls::getStr("moonlight/shortcuts/button_slot_3"),
        buttonLabels,
        comboButtonToSelection(editorState->combo, 2),
        [syncComboFromSelectors](int)
        {
            syncComboFromSelectors();
        });

    content->addView(button1Selector);
    content->addView(button2Selector);
    content->addView(button3Selector);

    auto* clearCell = new brls::DetailCell();
    clearCell->setText(brls::getStr("moonlight/shortcuts/clear_combo_title"));
    clearCell->setDetailText(brls::getStr("moonlight/shortcuts/clear_combo_detail"));
    clearCell->registerClickAction([editorState, button1Selector, button2Selector, button3Selector](brls::View*)
        {
        editorState->combo = shortcuts::ShortcutCombo{};
        button1Selector->setSelection(0, true);
        button2Selector->setSelection(0, true);
        button3Selector->setSelection(0, true);
        brls::Application::notify(brls::getStr("moonlight/shortcuts/notify_cleared"));
        return true; });
    content->addView(clearCell);

    if (canDelete)
    {
        auto* deleteCell = new brls::DetailCell();
        deleteCell->setText(brls::getStr("moonlight/shortcuts/delete_shortcut_title"));
        deleteCell->setDetailText(brls::getStr("moonlight/shortcuts/delete_shortcut_detail"));
        deleteCell->registerClickAction([this, index](brls::View*)
            {
            auto& managerRef = shortcuts::ShortcutManager::instance();
            if (managerRef.removeShortcut(index, true)) {
                brls::Application::popActivity(brls::TransitionAnimation::FADE, [this]() {
                    reload_shortcuts_config();
                    rebuildShortcutList();
                    auto children = shortcutsListContainer->getChildren();
                    if (!children.empty()) {
                        brls::Application::giveFocus(children.front());
                    }
                    brls::Application::notify(brls::getStr("moonlight/shortcuts/notify_deleted"));
                });
            }
            return true; });
        content->addView(deleteCell);
    }

    auto* saveCell = new brls::DetailCell();
    saveCell->setText(brls::getStr("moonlight/shortcuts/save_shortcut_title"));
    saveCell->setDetailText(brls::getStr("moonlight/shortcuts/save_shortcut_detail"));
    saveCell->registerClickAction([this, index, creatingNew, canChangeAction, editorState](brls::View*)
        {
        auto& managerRef = shortcuts::ShortcutManager::instance();
        bool saved = false;

        if (creatingNew) {
            if (!shortcuts::comboHasAnyButton(editorState->combo)) {
                brls::Application::notify(brls::getStr("moonlight/shortcuts/notify_invalid_combo"));
                return true;
            }
            saved = managerRef.addShortcut(*editorState, true);
        } else if (canChangeAction) {
            saved = managerRef.setShortcut(index, *editorState, true);
        } else {
            saved = managerRef.setShortcutCombo(index, editorState->combo, true);
        }

        if (saved) {
            brls::Application::popActivity(brls::TransitionAnimation::FADE, [this]() {
                reload_shortcuts_config();
                rebuildShortcutList();
                auto children = shortcutsListContainer->getChildren();
                if (!children.empty()) {
                    brls::Application::giveFocus(children.front());
                }
                brls::Application::notify(brls::getStr("moonlight/shortcuts/notify_saved"));
            });
        }
        return true; });
    content->addView(saveCell);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->addView(content);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle(creatingNew ? brls::getStr("moonlight/shortcuts/add_shortcut_title") : shortcutTitleForEntry(index, *editorState));
    brls::Application::pushActivity(new brls::Activity(frame));
}

brls::View* ShortcutsSettingsTab::create()
{
    return new ShortcutsSettingsTab();
}
