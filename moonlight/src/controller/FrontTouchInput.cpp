#include "controller/FrontTouchInput.hpp"

#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <string.h>

#include "controller/touch_zone_action.hpp"
#include "utils/math_utils.hpp"
#include "utils/vita_platform.hpp"

namespace
{
constexpr float PANEL_WIDTH  = 960.0f;
constexpr float PANEL_HEIGHT = 544.0f;
constexpr float MAX_TOUCH_X  = 1919.0f;
constexpr float MAX_TOUCH_Y  = 1087.0f;

}

FrontTouchInputManager::FrontTouchInputManager()
{
    recalcZones();
}

void FrontTouchInputManager::updateSettings(const VideoSettings& settings)
{
    enabled  = settings.enable_front_touchzones;
    offset   = settings.front_touch_offset;
    size     = settings.front_touch_size;
    actionNW = settings.front_action_northwest;
    actionNE = settings.front_action_northeast;
    actionSW = settings.front_action_southwest;
    actionSE = settings.front_action_southeast;
    recalcZones();
    lastZoneActive.fill(false);
}

void FrontTouchInputManager::setEnabled(bool value)
{
    if (enabled && !value)
    {
        dropState();
    }
    enabled = value;
}

void FrontTouchInputManager::recalcZones()
{
    // NW: top-left corner
    zones[0].left   = static_cast<float>(offset);
    zones[0].top    = static_cast<float>(offset);
    zones[0].right  = static_cast<float>(offset + size);
    zones[0].bottom = static_cast<float>(offset + size);

    // NE: top-right corner
    zones[1].left   = PANEL_WIDTH - static_cast<float>(offset + size);
    zones[1].top    = static_cast<float>(offset);
    zones[1].right  = PANEL_WIDTH - static_cast<float>(offset);
    zones[1].bottom = static_cast<float>(offset + size);

    // SW: bottom-left corner
    zones[2].left   = static_cast<float>(offset);
    zones[2].top    = PANEL_HEIGHT - static_cast<float>(offset + size);
    zones[2].right  = static_cast<float>(offset + size);
    zones[2].bottom = PANEL_HEIGHT - static_cast<float>(offset);

    // SE: bottom-right corner
    zones[3].left   = PANEL_WIDTH - static_cast<float>(offset + size);
    zones[3].top    = PANEL_HEIGHT - static_cast<float>(offset + size);
    zones[3].right  = PANEL_WIDTH - static_cast<float>(offset);
    zones[3].bottom = PANEL_HEIGHT - static_cast<float>(offset);
}

bool FrontTouchInputManager::process(GamepadState& state, bool isPstvModel)
{
    if (!enabled || isPstvModel)
    {
        return false;
    }

    static uint64_t lastFrontTouchPollUs = 0;
    uint64_t nowUs                       = vita::monotonicMicroseconds();
    // Poll front touch at 125Hz to reduce CPU cost without noticeable latency.
    if (lastFrontTouchPollUs != 0 && (nowUs - lastFrontTouchPollUs) < 8000)
    {
        return false;
    }
    lastFrontTouchPollUs = nowUs;

    SceTouchData frontData;
    memset(&frontData, 0, sizeof(SceTouchData));
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &frontData, 1);

    std::array<bool, 4> zoneActive { false, false, false, false };
    bool anyInZone = false;

    for (int i = 0; i < frontData.reportNum; ++i)
    {
        const SceTouchReport& report = frontData.report[i];
        const float x                = moonlight::clampNormalized(static_cast<float>(report.x) / MAX_TOUCH_X) * PANEL_WIDTH;
        const float y                = moonlight::clampNormalized(static_cast<float>(report.y) / MAX_TOUCH_Y) * PANEL_HEIGHT;

        for (std::size_t z = 0; z < zones.size(); ++z)
        {
            if (x >= zones[z].left && x <= zones[z].right && y >= zones[z].top && y <= zones[z].bottom)
            {
                zoneActive[z] = true;
                anyInZone     = true;
            }
        }
    }

    for (std::size_t i = 0; i < zoneActive.size(); ++i)
    {
        const std::uint32_t actionCode = getActionForIndex(i);
        controller::applyTouchZoneAction(zoneActive[i], lastZoneActive[i], actionCode, state);
        lastZoneActive[i] = zoneActive[i];
    }

    return anyInZone;
}

void FrontTouchInputManager::dropState()
{
    GamepadState dummy {};
    for (std::size_t i = 0; i < lastZoneActive.size(); ++i)
    {
        if (lastZoneActive[i])
        {
            controller::applyTouchZoneAction(false, true, getActionForIndex(i), dummy);
        }
    }
    lastZoneActive.fill(false);
}

std::uint32_t FrontTouchInputManager::getActionForIndex(std::size_t index) const
{
    switch (index)
    {
        case 0:
            return actionNW;
        case 1:
            return actionNE;
        case 2:
            return actionSW;
        case 3:
            return actionSE;
        default:
            return 0;
    }
}
