#pragma once

#include <psp2/touch.h>

#include <array>

#include "ConfigManager.hpp"
#include "controller/GamepadState.hpp"

class FrontTouchInputManager
{
  public:
    FrontTouchInputManager();

    void updateSettings(const VideoSettings& settings);
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled; }

    bool process(GamepadState& state, bool isPstvModel);
    void dropState();

  private:
    bool enabled           = false;
    int offset             = 0;
    int size               = 150;
    std::uint32_t actionNW = 0;
    std::uint32_t actionNE = 0;
    std::uint32_t actionSW = 0;
    std::uint32_t actionSE = 0;

    std::array<bool, 4> lastZoneActive { false, false, false, false };

    void recalcZones();
    std::uint32_t getActionForIndex(std::size_t index) const;

    struct Zone
    {
        float left, top, right, bottom;
    };
    std::array<Zone, 4> zones;
};
