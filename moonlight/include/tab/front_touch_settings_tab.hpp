#pragma once

#include <borealis.hpp>

class FrontTouchSettingsTab : public brls::Box
{
  public:
    FrontTouchSettingsTab();

    BRLS_BIND(brls::BooleanCell, frontTouchToggle, "frontTouchToggle");
    BRLS_BIND(brls::SliderCell, frontTouchOffsetSlider, "frontTouchOffsetSlider");
    BRLS_BIND(brls::SliderCell, frontTouchSizeSlider, "frontTouchSizeSlider");
    BRLS_BIND(brls::DetailCell, frontTouchPreviewCell, "frontTouchPreviewCell");
    BRLS_BIND(brls::SelectorCell, frontTouchNWSelector, "frontTouchNWSelector");
    BRLS_BIND(brls::SelectorCell, frontTouchNESelector, "frontTouchNESelector");
    BRLS_BIND(brls::SelectorCell, frontTouchSWSelector, "frontTouchSWSelector");
    BRLS_BIND(brls::SelectorCell, frontTouchSESelector, "frontTouchSESelector");

    static brls::View* create();
};
