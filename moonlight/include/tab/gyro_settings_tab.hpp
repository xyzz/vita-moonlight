#pragma once

#include <borealis.hpp>

class GyroSettingsTab : public brls::Box
{
  public:
    GyroSettingsTab();

    static brls::View* create();

  private:
    void updateScalarLabels();

    BRLS_BIND(brls::BooleanCell, motionToggle, "motionToggle");
    BRLS_BIND(brls::Slider, scalarXSlider, "scalarXSlider");
    BRLS_BIND(brls::Label, scalarXLabel, "scalarXLabel");
    BRLS_BIND(brls::Slider, scalarYSlider, "scalarYSlider");
    BRLS_BIND(brls::Label, scalarYLabel, "scalarYLabel");
    BRLS_BIND(brls::DetailCell, testCell, "testCell");
};
