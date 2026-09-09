#pragma once

#include <psp2/touch.h>

#include <array>

#include "ConfigManager.hpp"
#include "controller/GamepadState.hpp"

class RearTouchInputManager
{
  public:
    RearTouchInputManager();

    void updateSettings(const RearTouchSettings& settings);
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled; }

    void setSwapShoulderButtons(bool enabled);

    void process(GamepadState& state, bool isPstvModel);
    void dropState();

    const RearTouchSettings& getSettings() const { return currentSettings; }

  private:
    RearTouchSettings currentSettings {};
    bool enabled             = true;
    bool swapShoulderButtons = false;

    std::array<bool, 4> lastZoneActive { false, false, false, false };

    float leftNorm   = 0.0f;
    float rightNorm  = 1.0f;
    float topNorm    = 0.0f;
    float bottomNorm = 1.0f;
    float midX       = 0.5f;
    float midY       = 0.5f;

    void recalcBounds();
    std::uint32_t getActionForIndex(std::size_t index) const;
};
