#pragma once

#include <borealis.hpp>

class TrackpadSettingsTab : public brls::Box
{
  public:
    TrackpadSettingsTab();

    BRLS_BIND(brls::SliderCell, pointerSpeedSlider, "pointerSpeedSlider");
    BRLS_BIND(brls::BooleanCell, twoFingerRightClickToggle, "twoFingerRightClickToggle");
    BRLS_BIND(brls::BooleanCell, twoFingerScrollToggle, "twoFingerScrollToggle");
    BRLS_BIND(brls::BooleanCell, invertScrollToggle, "invertScrollToggle");
    BRLS_BIND(brls::BooleanCell, multiTouchGesturesToggle, "multiTouchGesturesToggle");
    BRLS_BIND(brls::SliderCell, edgeZoneSlider, "edgeZoneSlider");
    BRLS_BIND(brls::SliderCell, deadZoneSlider, "deadZoneSlider");

    static brls::View* create();

  private:
    void applyTrackpadSettingsLive();
};
