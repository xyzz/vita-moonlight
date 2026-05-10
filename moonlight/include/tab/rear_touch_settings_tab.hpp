#pragma once

#include <borealis.hpp>

class RearTouchSettingsTab : public brls::Box
{
  public:
    RearTouchSettingsTab();

    BRLS_BIND(brls::BooleanCell, rearTouchToggle, "rearTouchToggle");
    BRLS_BIND(brls::DetailCell, rearTouchCalibrationCell, "rearTouchCalibrationCell");
    BRLS_BIND(brls::SelectorCell, rearTouchNWSelector, "rearTouchNWSelector");
    BRLS_BIND(brls::SelectorCell, rearTouchNESelector, "rearTouchNESelector");
    BRLS_BIND(brls::SelectorCell, rearTouchSWSelector, "rearTouchSWSelector");
    BRLS_BIND(brls::SelectorCell, rearTouchSESelector, "rearTouchSESelector");
    BRLS_BIND(brls::Label, rearTouchSwapWarning, "rearTouchSwapWarning");

    static brls::View* create();
};
