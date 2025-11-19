#pragma once

#include <borealis.hpp>
#include <vector>

class TestOverlayStream : public brls::View
{
  public:
    TestOverlayStream();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    void onLayout() override;
    brls::View* getDefaultFocus() override { return this; }
    const char* describe() const { return "TestOverlayStream"; }

  private:
    int focusedIndex = 0;
    std::vector<std::string> buttonLabels;
    void moveFocus(int delta);
    void activateFocused();
};
