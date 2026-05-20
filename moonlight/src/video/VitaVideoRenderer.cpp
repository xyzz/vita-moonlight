// VitaVideoRenderer.cpp - Unified implementation (shared + NVG + vita2d)
#include "VitaVideoRenderer.hpp"

#include <borealis/extern/nanovg/nanovg.h>
#include <borealis/extern/nanovg/nanovg_gxm.h>
#include <borealis/extern/nanovg/nanovg_gxm_utils.h>
#include <psp2/gxm.h>
#include <stdlib.h>

#include <borealis/core/application.hpp>
#include <mutex>

#include "legacy/modules/vita_globals.hpp"

namespace
{
uint32_t s_presentWindowFrames = 0;
uint64_t s_cachedStatsStart    = 0;
uint32_t s_prevDecodedCount    = 0;

bool is_gpu_yuv_experimental_enabled()
{
    static int cached = -1;
    if (cached == -1)
    {
        const char* env      = getenv("MOONLIGHT_FFMPEG_GPU_YUV");
        bool envEnabled      = (env && env[0] == '1');
        bool settingsEnabled = (g_video_settings_snapshot.pixel_format_mode == 1);
        cached               = (envEnabled || settingsEnabled) ? 1 : 0;
        if (cached)
        {
            VITA_DEBUG_LOG("[Video][NVG][EXP] GPU YUV experimental path enabled (env=%d settings_pixel_format_mode=%d)",
                envEnabled ? 1 : 0,
                (int)g_video_settings_snapshot.pixel_format_mode);
        }
    }
    return cached == 1;
}

bool is_yuv_gxm_format(uint32_t fmt)
{
    return fmt == (uint32_t)SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0 || fmt == (uint32_t)SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0;
}
}

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
    if (vg)
    {
        drawNVG(vg, viewportW, viewportH, alpha);
    }
    else
    {
        draw(viewportW, viewportH);
    }
}

void VitaVideoRenderer::draw(float viewportW, float viewportH)
{
    // Non-NVG draw path — no longer supported without vita2d
    // All rendering goes through drawNVG via Borealis
    static bool logged = false;
    if (!logged)
    {
        VITA_DEBUG_LOG("[Video][DRAW] Non-NVG draw path called, skipping (vita2d removed)");
        logged = true;
    }
    return;
    if (g_stats.frames_decoded == 0)
    {
        static bool logged = false;
        if (!logged)
        {
            VITA_DEBUG_LOG("[Video][DRAW] primer frame aun no decodificado - skip");
            logged = true;
        }
        return;
    }

    GxmTexture* tex = nullptr;
    int frontIdx    = 0;
    int backIdx     = 0;
    {
        std::lock_guard<std::mutex> slotLock(g_frame_slots_mutex);
        tex      = FRAME_FRONT();
        frontIdx = frame_front_idx;
        backIdx  = frame_back_idx;
    }

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

    const SceGxmTexture* gxmTex = &tex->gxm_tex;
    uint32_t currentFmt         = (uint32_t)sceGxmTextureGetFormat(const_cast<SceGxmTexture*>(gxmTex));
    if (currentFmt != (uint32_t)SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR)
    {
        static uint32_t nonRgbaDrawDropCounter = 0;
        if ((nonRgbaDrawDropCounter++ % 120) == 0)
        {
            VITA_DEBUG_LOG("[Video][DRAW][SAFE] skip non-RGBA texture fmt=0x%08X", (unsigned)currentFmt);
        }
        return;
    }

    static uint32_t drawCounter = 0;
    if (drawCounter < 120 || (drawCounter % 60) == 0)
    {
        unsigned stride = gxm_texture_get_stride(tex);
        VITA_DEBUG_LOG("[Video][DRAW] frame=%u tex=%p stride=%u frontIdx=%d backIdx=%d", drawCounter, tex, stride, frontIdx, backIdx);
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

    float srcX = image_scaling.region_x1;
    float srcY = image_scaling.region_y1;
    float srcW = image_scaling.region_x2 - image_scaling.region_x1;
    float srcH = image_scaling.region_y2 - image_scaling.region_y1;

    const float texW = (float)gxm_texture_get_width(tex);
    const float texH = (float)gxm_texture_get_height(tex);
    if (texW <= 0.f || texH <= 0.f)
        return;

    if (srcX < 0.f)
    {
        srcW += srcX;
        srcX = 0.f;
    }
    if (srcY < 0.f)
    {
        srcH += srcY;
        srcY = 0.f;
    }
    if (srcX >= texW || srcY >= texH)
        return;
    if (srcX + srcW > texW)
        srcW = texW - srcX;
    if (srcY + srcH > texH)
        srcH = texH - srcY;
    if (srcW <= 0.f || srcH <= 0.f)
        return;

    float scaleX = (float)dw / image_scaling.texture_width;
    float scaleY = (float)dh / image_scaling.texture_height;
    if (scaleX <= 0.f || scaleY <= 0.f)
        return;

    // vita2d_draw removed — use NVG path instead
    // vita2d_draw_texture_tint_part_scale not available

    g_stats.frames_presented++;
    onFramePresented();
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

    const GxmTexture* tex = nullptr;
    {
        std::lock_guard<std::mutex> slotLock(g_frame_slots_mutex);
        tex = FRAME_FRONT();
    }
    if (!tex)
        return;

    const SceGxmTexture* gxmTex = &tex->gxm_tex;
    const void* currentData     = sceGxmTextureGetData(const_cast<SceGxmTexture*>(gxmTex));
    uint32_t currentFmt         = (uint32_t)sceGxmTextureGetFormat(const_cast<SceGxmTexture*>(gxmTex));
    bool isYuvTexture           = is_yuv_gxm_format(currentFmt);
    if (currentFmt != (uint32_t)SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR && !isYuvTexture)
    {
        static uint32_t yuvFallbackLogCounter = 0;
        if ((yuvFallbackLogCounter++ % 180) == 0)
        {
            VITA_DEBUG_LOG("[Video][NVG][SAFE] formato 0x%08X no-RGBA/no-YUV; skip frame", (unsigned)currentFmt);
        }
        return;
    }

    if (isYuvTexture)
    {
        static uint32_t yuvExpLogCounter = 0;
        if ((yuvExpLogCounter++ % 1200) == 0)
        {
            VITA_DEBUG_LOG("[Video][NVG] rendering YUV/NV12 texture fmt=0x%08X via GXM CSC", (unsigned)currentFmt);
        }
        NVGXMwindow* win = gxmGetWindow();
        if (win && win->context)
        {
            sceGxmSetYuvProfile(win->context, 0, SCE_GXM_YUV_PROFILE_BT709_STANDARD);
        }
    }

    static uint32_t nvgDiagCounter = 0;
    if ((nvgDiagCounter++ % 1200) == 0)
    {
        VITA_DEBUG_LOG("[Video][NVG][DIAG] tex=%p gxm=%p fmt=0x%08X data=%p", tex, gxmTex, (unsigned int)currentFmt, currentData);
    }

    if (!image_scaling.enabled)
        return;

    uint32_t texW = image_scaling.texture_width;
    uint32_t texH = image_scaling.texture_height;
    if (texW == 0 || texH == 0)
        return;

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
        if (imageCache[foundIdx].width != (int)texW || imageCache[foundIdx].height != (int)texH || imageCache[foundIdx].format != currentFmt || imageCache[foundIdx].data != currentData)
        {
            nvgDeleteImage(vg, imageCache[foundIdx].imageId);
            int newId = nvgxmCreateImageFromHandle(vg, const_cast<SceGxmTexture*>(gxmTex));
            if (newId > 0)
            {
                imageCache[foundIdx].imageId    = newId;
                imageCache[foundIdx].width      = (int)texW;
                imageCache[foundIdx].height     = (int)texH;
                imageCache[foundIdx].data       = currentData;
                imageCache[foundIdx].format     = currentFmt;
                useImageId                      = newId;
                static uint32_t recreateCounter = 0;
                if ((recreateCounter++ % 600) == 0)
                {
                    VITA_DEBUG_LOG("[Video][NVG] Recreated cached image %d due to VRAM reuse (data=%p)", newId, currentData);
                }
            }
        }
        else
        {
            useImageId = imageCache[foundIdx].imageId;
        }
    }
    else
    {
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
                imageCache[idx].data    = currentData;
                imageCache[idx].format  = currentFmt;
                useImageId              = newId;
                VITA_DEBUG_LOG("[Video][NVG] Cached new image %d (slot %d)", newId, idx);
            }
            else
            {
                VITA_DEBUG_LOG("[Video][NVG] Cache full, evicting slot 0");
                nvgDeleteImage(vg, imageCache[0].imageId);
                imageCache[0].tex     = tex;
                imageCache[0].imageId = newId;
                imageCache[0].width   = (int)texW;
                imageCache[0].height  = (int)texH;
                imageCache[0].data    = currentData;
                imageCache[0].format  = currentFmt;
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
    onFramePresented();
}

void VitaVideoRenderer::destroyImage(NVGcontext* vg)
{
    if (!vg)
    {
        vg = brls::Application::getNVGContext();
    }

    if (vg)
    {
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

extern "C" void ffmpeg_process_deferred_releases(void);
extern "C" void ffmpeg_increment_presented_frames(void);

void VitaVideoRenderer::onFramePresented()
{
    ffmpeg_process_deferred_releases();
    ffmpeg_increment_presented_frames();
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
