// VitaVideoRenderer.hpp - Simplified centralized renderer (NVG only, no vita2d)
#pragma once

#include <stdint.h>

#include <atomic>

#include "video/gxm_texture.hpp"
struct NVGcontext; // forward

class VitaVideoRenderer final
{
  public:
    static VitaVideoRenderer& instance();
    void draw(float viewportW, float viewportH);
    // Support for old call that included NVGcontext/alpha
    void draw(struct NVGcontext* vg, float viewportW, float viewportH, float alpha = 1.0f);
    // Draw using NanoVG with Borealis GXM context
    void drawNVG(struct NVGcontext* vg, float viewportW, float viewportH, float alpha = 1.0f);
    void setFullscreenStretch(bool stretch);
    bool isFullscreenStretch() const { return fullscreenStretch; }
    void setKeyboardViewport(int visibleHeight, bool showBottom);
    void setKeyboardFocus(float normalizedY);
    void clearKeyboardViewport();
    void adjustPresentation(float& x, float& y, float width, float height) const;
    void cropKeyboardPresentation(float& y, float& height, float& texTop, float& texBottom) const;
    // Release NVG image that references the current GXM texture.
    // Public so UI code can clear any NVG references before stopping video.
    void destroyImage(struct NVGcontext* vg);
    void onFramePresented(const void* textureData = nullptr);

  private:
    VitaVideoRenderer()    = default;
    bool fullscreenStretch = true; // synchronized with global video_fullscreen_stretch
    // Resources for NVG image (direct GXM texture)
    struct NvgImageCacheEntry
    {
        const GxmTexture* tex = nullptr;
        int imageId           = -1;
        int width             = 0;
        int height            = 0;
        const void* data      = nullptr;
        uint32_t format       = 0;
    };

    // Simple cache (max 2-3 entries usually)
    NvgImageCacheEntry imageCache[4];
    int imageCacheSize = 0;
    std::atomic<int> keyboardViewportHeight { 0 };
    std::atomic<bool> keyboardShowBottom { false };
    std::atomic<int> keyboardFocusY { -1 };

    // Diagnostics
    uint32_t nvgImageCreateCount = 0;
};
