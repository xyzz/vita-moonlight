#pragma once
#include <borealis.hpp>

#include "utils/overlay_utils.hpp"
#include "video/legacy/vita.hpp"

class VitaStreamOverlayView : public BaseOverlay
{
  public:
    VitaStreamOverlayView();
    ~VitaStreamOverlayView() override = default;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    void onLayout() override;
    brls::View* getDefaultFocus() override { return nullptr; }
    const char* describe() const { return "VitaStreamOverlayView"; }

  private:
    uint64_t lastFetchMs = 0;
    VitaVideoStats cached;
};
