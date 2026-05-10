#pragma once

#include <psp2/touch.h>

#include <borealis.hpp>

class FrontTouchPreviewCanvas : public brls::View
{
  public:
    explicit FrontTouchPreviewCanvas(int offset, int size);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override { return this; }

    void setOffset(int offset);
    void setSize(int size);

  private:
    int offset = 0;
    int size   = 150;

    void drawZone(NVGcontext* vg, float x, float y, float w, float h, const char* label, bool active);
};

class FrontTouchPreviewOverlay : public brls::AppletFrame
{
  public:
    FrontTouchPreviewOverlay(int offset, int size);
    ~FrontTouchPreviewOverlay() override;

  private:
    FrontTouchPreviewCanvas* canvas = nullptr;

#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    SceTouchSamplingState previousFrontSamplingState = SCE_TOUCH_SAMPLING_STATE_STOP;
    bool restoreSamplingState                        = false;
#endif
};
