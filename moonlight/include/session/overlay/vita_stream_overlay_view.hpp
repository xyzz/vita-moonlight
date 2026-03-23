#pragma once
#include <borealis.hpp>

#include "utils/overlay_utils.hpp"
#include "video/legacy/vita.hpp"

// Simple overlay view to display video statistics on Vita.
// In the future it can be expanded with controls (pause, bitrate, etc.).
class VitaStreamOverlayView : public BaseOverlay
{
  public:
    VitaStreamOverlayView();
    ~VitaStreamOverlayView() override = default;

    // View interface implementation
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    void onLayout() override; // used instead of layout(...)
    brls::View* getDefaultFocus() override { return nullptr; }
    const char* describe() const { return "VitaStreamOverlayView"; }

  private:
    uint64_t lastFetchMs = 0;
    VitaVideoStats cached; // is initialized in constructor via vitavideo_get_stats
};
