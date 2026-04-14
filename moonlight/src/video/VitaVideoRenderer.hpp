// VitaVideoRenderer.hpp - Simplified centralized renderer (without NVG staging)
#pragma once

#include <stdint.h>
#include <vita2d.h>
extern bool vita2d_inited; // defined in vita_globals.cpp
struct NVGcontext; // forward

// We do not depend on NanoVG for rendering (vita2d only)

class VitaVideoRenderer final
{
  public:
    static VitaVideoRenderer& instance();
    void draw(float viewportW, float viewportH);
    // Support for old call that included NVGcontext/alpha
    void draw(struct NVGcontext* vg, float viewportW, float viewportH, float alpha = 1.0f);
    // New path: draw using NanoVG (avoid duplicate begin/end of vita2d)
    void drawNVG(struct NVGcontext* vg, float viewportW, float viewportH, float alpha = 1.0f);
    void setFullscreenStretch(bool stretch);
    bool isFullscreenStretch() const { return fullscreenStretch; }
    // Release NVG image that references the current vita2d texture.
    // Public so UI code can clear any NVG references before stopping video.
    void destroyImage(struct NVGcontext* vg);

  private:
    VitaVideoRenderer() = default;
    void onFramePresented();
    bool fullscreenStretch = true; // synchronized with global video_fullscreen_stretch
    // Resources for NVG image (direct GXM texture)
    struct NvgImageCacheEntry
    {
        const vita2d_texture* tex = nullptr;
        int imageId               = -1;
        int width                 = 0;
        int height                = 0;
        const void* data          = nullptr;
        uint32_t format           = 0;
    };

    // Simple cache (max 2-3 entries usually)
    NvgImageCacheEntry imageCache[4];
    int imageCacheSize = 0;

    // Diagnostics
    uint32_t nvgImageCreateCount = 0;
};
