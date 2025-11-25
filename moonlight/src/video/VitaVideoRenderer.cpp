// VitaVideoRenderer.cpp - Versión mínima sin zero-copy ni VideoPlane
#include "VitaVideoRenderer.hpp"

#include <borealis/extern/nanovg/nanovg.h>
#include <borealis/extern/nanovg/nanovg_gxm.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include <borealis/core/application.hpp>
#include <borealis/views/label.hpp>

#include "legacy/modules/vita_globals.hpp"

namespace
{
uint32_t s_presentWindowFrames = 0;
uint64_t s_cachedStatsStart    = 0;
uint32_t s_prevDecodedCount    = 0;

void update_present_stats()
{
    uint64_t now = vita_monotonic_ms();
    if (stats_start_ms == 0)
    {
        stats_start_ms = now;
    }

    if (stats_start_ms != 0 && stats_start_ms != s_cachedStatsStart)
    {
        s_cachedStatsStart    = stats_start_ms;
        s_presentWindowFrames = 0;
        last_fps_window_ms    = stats_start_ms;
        // Reset decoding window counter in sync with the present window for easier comparison
        s_prevDecodedCount = g_stats.frames_decoded;
    }

    if (last_fps_window_ms == 0)
    {
        last_fps_window_ms = now;
    }

    s_presentWindowFrames++;

    if (stats_start_ms <= now)
    {
        g_stats.session_ms = now - stats_start_ms;
    }

    uint64_t elapsed = now - last_fps_window_ms;
    if (elapsed >= 1000)
    {
        if (elapsed == 0)
            elapsed = 1;
        g_stats.current_fps = (uint32_t)((uint64_t)s_presentWindowFrames * 1000ULL / elapsed);
        curr_fps[0]         = g_stats.current_fps;
        // Compute decode FPS using the global decoded frames counter and the same time window
        uint32_t nowDecCount  = g_stats.frames_decoded;
        uint32_t decInWindow  = nowDecCount - s_prevDecodedCount;
        g_stats.decoded_fps   = (uint32_t)((uint64_t)decInWindow * 1000ULL / elapsed);
        s_prevDecodedCount    = nowDecCount;
        last_fps_window_ms    = now;
        s_presentWindowFrames = 0;
    }
}
}

// Implementación

VitaVideoRenderer& VitaVideoRenderer::instance()
{
    static VitaVideoRenderer inst;
    return inst;
}

void VitaVideoRenderer::setFullscreenStretch(bool stretch)
{
    fullscreenStretch        = stretch;
    video_fullscreen_stretch = stretch;
}

void VitaVideoRenderer::draw(struct NVGcontext* vg, float viewportW, float viewportH, float alpha)
{
    (void)vg;
    (void)alpha;
    draw(viewportW, viewportH);
}

void VitaVideoRenderer::draw(float viewportW, float viewportH)
{
    if (!vita2d_inited)
    {
        static bool logged = false;
        if (!logged)
        {
            VITA_DEBUG_LOG("[Video][DRAW] vita2d no inicializado");
            logged = true;
        }
        return;
    }
    if (g_stats.frames_decoded == 0)
    {
        static bool logged = false;
        if (!logged)
        {
            VITA_DEBUG_LOG("[Video][DRAW] primer frame aún no decodificado - skip");
            logged = true;
        }
        return;
    }
    vita2d_texture* tex = FRAME_FRONT();
    if (!tex)
    {
        static bool logged = false;
        if (!logged)
        {
            VITA_DEBUG_LOG("[Video][DRAW] FRAME_FRONT null");
            logged = true;
        }
        return;
    }
    static uint32_t drawCounter = 0;
    if (drawCounter < 120 || (drawCounter % 60) == 0)
    {
        unsigned stride = vita2d_texture_get_stride(tex);
        VITA_DEBUG_LOG("[Video][DRAW] frame=%u tex=%p stride=%u frontIdx=%d backIdx=%d", drawCounter, tex, stride, frame_front_idx, frame_back_idx);
    }
    drawCounter++;
    if (!image_scaling.enabled)
        return;
    int dw = fullscreenStretch ? (int)viewportW : image_scaling.display_width;
    int dh = fullscreenStretch ? (int)viewportH : image_scaling.display_height;
    int ox = fullscreenStretch ? 0 : image_scaling.offset_x;
    int oy = fullscreenStretch ? 0 : image_scaling.offset_y;
    if (dw <= 0 || dh <= 0)
        return;
    if (image_scaling.region_x2 <= image_scaling.region_x1 || image_scaling.region_y2 <= image_scaling.region_y1)
        return;
    if (image_scaling.texture_width == 0 || image_scaling.texture_height == 0)
        return;
    float scaleX = (float)dw / image_scaling.texture_width;
    float scaleY = (float)dh / image_scaling.texture_height;
    if (scaleX <= 0.f || scaleY <= 0.f)
        return;
    vita2d_draw_texture_tint_part_scale(
        tex,
        (float)ox, (float)oy,
        image_scaling.region_x1, image_scaling.region_y1,
        image_scaling.region_x2, image_scaling.region_y2,
        scaleX,
        scaleY,
        0xFFFFFFFF);
    g_stats.frames_presented++;
    update_present_stats();
}

void VitaVideoRenderer::drawNVG(NVGcontext* vg, float viewportW, float viewportH, float alpha)
{
    if (!vg)
    {
        draw(viewportW, viewportH);
        return;
    }
    if (g_stats.frames_decoded == 0)
        return;
    const vita2d_texture* tex = FRAME_FRONT();
    if (!tex)
        return;
    const SceGxmTexture* gxmTex = &tex->gxm_tex;

    if (!image_scaling.enabled)
        return;

    uint32_t texW = image_scaling.texture_width;
    uint32_t texH = image_scaling.texture_height;
    if (texW == 0 || texH == 0)
        return;

    // [OPTIMIZATION] Image Cache Lookup
    int foundIdx = -1;
    for (int i = 0; i < imageCacheSize; i++)
    {
        if (imageCache[i].tex == tex)
        {
            foundIdx = i;
            break;
        }
    }

    int useImageId = -1;

    if (foundIdx >= 0)
    {
        // Found in cache, check if valid
        if (imageCache[foundIdx].width != (int)texW || imageCache[foundIdx].height != (int)texH)
        {
            // Dimensions changed, recreate
            nvgDeleteImage(vg, imageCache[foundIdx].imageId);
            int newId = nvgxmCreateImageFromHandle(vg, const_cast<SceGxmTexture*>(gxmTex));
            if (newId > 0)
            {
                imageCache[foundIdx].imageId = newId;
                imageCache[foundIdx].width   = (int)texW;
                imageCache[foundIdx].height  = (int)texH;
                useImageId                   = newId;
                VITA_DEBUG_LOG("[Video][NVG] Recreated cached image %d (size change)", newId);
            }
        }
        else
        {
            useImageId = imageCache[foundIdx].imageId;
        }
    }
    else
    {
        // Not found, create new
        int newId = nvgxmCreateImageFromHandle(vg, const_cast<SceGxmTexture*>(gxmTex));
        if (newId > 0)
        {
            if (imageCacheSize < 4)
            {
                int idx                 = imageCacheSize++;
                imageCache[idx].tex     = tex;
                imageCache[idx].imageId = newId;
                imageCache[idx].width   = (int)texW;
                imageCache[idx].height  = (int)texH;
                useImageId              = newId;
                VITA_DEBUG_LOG("[Video][NVG] Cached new image %d (slot %d)", newId, idx);
            }
            else
            {
                // Cache full (unlikely with double buffering), evict slot 0
                VITA_DEBUG_LOG("[Video][NVG] Cache full, evicting slot 0");
                nvgDeleteImage(vg, imageCache[0].imageId);
                imageCache[0].tex     = tex;
                imageCache[0].imageId = newId;
                imageCache[0].width   = (int)texW;
                imageCache[0].height  = (int)texH;
                useImageId            = newId;
            }
            nvgImageCreateCount++;
        }
    }

    if (useImageId <= 0)
    {
        VITA_DEBUG_LOG("[Video][DRAW NVG][ERR] Failed to get/create image for tex=%p", tex);
        return;
    }

    // Dibujar con la imagen NVG persistente
    int dw = fullscreenStretch ? (int)viewportW : image_scaling.display_width;
    int dh = fullscreenStretch ? (int)viewportH : image_scaling.display_height;
    int ox = fullscreenStretch ? 0 : image_scaling.offset_x;
    int oy = fullscreenStretch ? 0 : image_scaling.offset_y;
    if (dw <= 0 || dh <= 0)
        return;

    NVGpaint paint = nvgImagePattern(vg, (float)ox, (float)oy, (float)dw, (float)dh, 0.0f, useImageId, alpha);
    nvgBeginPath(vg);
    nvgRect(vg, (float)ox, (float)oy, (float)dw, (float)dh);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    g_stats.frames_presented++;
    update_present_stats();

    // Sincronizar GPU de manera explícita cada N frames para evitar acumulación de trabajo
    static uint32_t gpuSyncCounter = 0;
    if (++gpuSyncCounter % 30 == 0)
    { // Cada 30 frames (~500ms a 60fps)
        if (vita2d_inited)
        {
            vita2d_wait_rendering_done();
        }
    }
}

void VitaVideoRenderer::destroyImage(NVGcontext* vg)
{
    if (vg)
    {
        // Ensure any GPU rendering referencing the image is finished.
        if (vita2d_inited)
        {
            vita2d_wait_rendering_done();
        }
        for (int i = 0; i < imageCacheSize; i++)
        {
            if (imageCache[i].imageId >= 0)
            {
                nvgDeleteImage(vg, imageCache[i].imageId);
            }
        }
    }
    imageCacheSize = 0;
}
