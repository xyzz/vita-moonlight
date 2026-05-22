#include "session/hotkey_manager.hpp"

#include "borealis.hpp"
#include "debug.hpp"

HotkeyManager& HotkeyManager::instance()
{
    static HotkeyManager inst;
    return inst;
}

void HotkeyManager::onButtonEvent(brls::ControllerButton button, bool pressed)
{
    auto now = Clock::now();
    switch (button)
    {
        case brls::ControllerButton::BUTTON_START:
            btnStart = pressed;
            if (pressed)
                lastStart = now;
            // START alone does not activate anything, only for combos
            break;
        case brls::ControllerButton::BUTTON_LB:
            btnL = pressed;
            if (pressed)
                lastL = now;
            break;
        case brls::ControllerButton::BUTTON_RB:
            btnR = pressed;
            if (pressed)
                lastR = now;
            break;
        default:
            break;
    }
    if (pressed)
        tryTrigger();
}

void HotkeyManager::tryTrigger()
{
    if (!(btnStart && btnL && btnR))
        return;
    // Check time window
    auto now     = Clock::now();
    auto msStart = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStart).count();
    auto msL     = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastL).count();
    auto msR     = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastR).count();

    if (msStart <= comboWindowMs && msL <= comboWindowMs && msR <= comboWindowMs)
    {
        vita_log::info("[HotkeyManager] Pause combo START+L+R detectada");
        if (pauseCallback)
            pauseCallback();
    }
}
