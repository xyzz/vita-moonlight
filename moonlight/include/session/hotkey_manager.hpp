#pragma once
#include <chrono>
#include <functional>
#include <vector>

#include "borealis.hpp"

class HotkeyManager
{
  public:
    static HotkeyManager& instance();

    void setPauseCallback(const std::function<void()>& cb) { pauseCallback = cb; }

    // Called from a central input hook (for now it will be invoked manually from views).
    void onButtonEvent(brls::ControllerButton button, bool pressed);

    void setComboWindowMs(int ms) { comboWindowMs = ms; }

  private:
    HotkeyManager()                                = default;
    HotkeyManager(const HotkeyManager&)            = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    std::function<void()> pauseCallback;
    std::function<void()> menuCallback;

    bool btnStart = false;
    bool btnL     = false;
    bool btnR     = false;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastStart {};
    Clock::time_point lastL {};
    Clock::time_point lastR {};

    int comboWindowMs = 220;

    void tryTrigger();
};
