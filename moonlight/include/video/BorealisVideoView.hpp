#pragma once
#include <vita2d.h>

#include <atomic>
#include <borealis/core/frame_context.hpp>
#include <borealis/core/style.hpp>
#include <borealis/core/view.hpp>
#include <cstdint>

#include "video/VideoFrameHolder.hpp"

// Forward de NanoVG
struct NVGcontext;

class BorealisVideoView : public brls::View
{
  public:
    static BorealisVideoView* instance();
    BorealisVideoView();
    ~BorealisVideoView() override;

    // (Deprecated) submitFrame: mantenido temporalmente para compatibilidad pero ya no usado
    void submitFrame(const uint8_t* src, uint32_t w, uint32_t h, uint32_t pitchBytes) { }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override { return nullptr; }
    // No override de getPreferredSize: dejamos que el layout de Borealis nos dimensione.

  private:
    uint32_t storedW = 0, storedH = 0;
    int nvgImageId                       = -1;
    bool recreateImage                   = false;
    const vita2d_texture* currentTexture = nullptr;
    // Métricas simples
    uint64_t lastStatsMs  = 0;
    uint32_t framesDrawn  = 0;
    uint32_t framesSeen   = 0;
    uint64_t lastFramePts = 0;
    void destroyImage(NVGcontext* vg);
};
