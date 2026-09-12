#include "VitaVideoRenderer.hpp"

#include <borealis/extern/nanovg/nanovg.h>
#include <borealis/extern/nanovg/nanovg_gxm.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>

#include <algorithm>
#include <borealis/core/application.hpp>
#include <mutex>

#include "legacy/modules/vita_globals.hpp"
#include "session/connection_telemetry.hpp"
#include "video/runtime_settings.hpp"

namespace
{
uint32_t s_presentWindowFrames  = 0;
uint32_t s_presentWindowRepeats = 0;
uint64_t s_cachedStatsStart     = 0;
uint32_t s_prevDecodedCount     = 0;

bool is_yuv_gxm_format(uint32_t fmt)
{
    return fmt == (uint32_t)SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0 || fmt == (uint32_t)SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0;
}

uint32_t s_publish_to_render_sum_us = 0;
uint32_t s_publish_to_render_min_us = 999999;
uint32_t s_publish_to_render_max_us = 0;
uint32_t s_publish_to_render_count  = 0;
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

void VitaVideoRenderer::setKeyboardViewport(int visibleHeight, bool showBottom)
{
    keyboardViewportHeight.store(std::min(SCREEN_HEIGHT, std::max(0, visibleHeight)), std::memory_order_release);
    keyboardShowBottom.store(showBottom, std::memory_order_release);
}

void VitaVideoRenderer::clearKeyboardViewport()
{
    keyboardViewportHeight.store(0, std::memory_order_release);
    keyboardShowBottom.store(false, std::memory_order_release);
}

void VitaVideoRenderer::adjustPresentation(float& y) const
{
    const int visibleHeight = keyboardViewportHeight.load(std::memory_order_acquire);
    if (visibleHeight <= 0)
    {
        return;
    }

    if (keyboardShowBottom.load(std::memory_order_acquire))
    {
        y -= static_cast<float>(SCREEN_HEIGHT - visibleHeight);
    }
}

void VitaVideoRenderer::cropKeyboardPresentation(float& y, float& height, float& texTop, float& texBottom) const
{
    const int visibleHeight = keyboardViewportHeight.load(std::memory_order_acquire);
    if (visibleHeight <= 0 || height <= 0.0f)
    {
        return;
    }

    const float originalY      = y;
    const float originalHeight = height;
    const float clippedTop     = std::max(0.0f, originalY);
    const float clippedBottom  = std::min(static_cast<float>(visibleHeight), originalY + originalHeight);
    if (clippedBottom <= clippedTop)
    {
        height    = 0.0f;
        texBottom = texTop;
        return;
    }

    const float texRange = texBottom - texTop;
    texBottom            = texTop + texRange * ((clippedBottom - originalY) / originalHeight);
    texTop += texRange * ((clippedTop - originalY) / originalHeight);
    y      = clippedTop;
    height = clippedBottom - clippedTop;
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

void VitaVideoRenderer::draw(float, float)
{
    static bool logged = false;
    if (!logged)
    {
        VITA_DEBUG_LOG("[Video][DRAW] Non-NVG draw path called, skipping (vita2d removed)");
        logged = true;
    }
}

void VitaVideoRenderer::drawNVG(NVGcontext* vg, float viewportW, float viewportH, float alpha)
{
    if (!vg)
    {
        draw(viewportW, viewportH);
        return;
    }
    if (video_stats::load(g_stats.frames_decoded) == 0)
        return;

    VideoFrameSnapshot frame {};
    if (!video_frame_acquire_for_render(&frame))
        return;
    const GxmTexture* tex = frame.texture;

    SceGxmTexture gxmTexSnapshot = tex->gxm_tex;
    const SceGxmTexture* gxmTex  = &gxmTexSnapshot;
    const void* currentData      = sceGxmTextureGetData(const_cast<SceGxmTexture*>(gxmTex));
    uint32_t currentFmt          = (uint32_t)sceGxmTextureGetFormat(const_cast<SceGxmTexture*>(gxmTex));
    bool isYuvTexture            = is_yuv_gxm_format(currentFmt);
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
        static bool yuvProfileSet = false;
        if (!yuvProfileSet)
        {
            NVGXMwindow* win = gxmGetWindow();
            if (win && win->context)
            {
                sceGxmSetYuvProfile(win->context, 0, SCE_GXM_YUV_PROFILE_BT709_STANDARD);
                yuvProfileSet = true;
                VITA_DEBUG_LOG("[Video][NVG] YUV profile BT709 set (one-time)");
            }
        }
    }

    if (!image_scaling.enabled)
        return;

    uint32_t texW = image_scaling.texture_width;
    uint32_t texH = image_scaling.texture_height;
    if (texW == 0 || texH == 0)
        return;

    // nvgxmCreateImageFromHandle copies the texture descriptor, so update the
    // cached descriptor when FFmpeg rotates a new VRAM frame into the same slot.
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
        bool needRecreate = (imageCache[foundIdx].width != (int)texW || imageCache[foundIdx].height != (int)texH || imageCache[foundIdx].format != currentFmt);
        if (needRecreate)
        {
            nvgDeleteImage(vg, imageCache[foundIdx].imageId);
            int newId = nvgxmCreateImageFromHandle(vg, const_cast<SceGxmTexture*>(gxmTex));
            if (newId > 0)
            {
                imageCache[foundIdx].imageId = newId;
                imageCache[foundIdx].width   = (int)texW;
                imageCache[foundIdx].height  = (int)texH;
                imageCache[foundIdx].data    = currentData;
                imageCache[foundIdx].format  = currentFmt;
                useImageId                   = newId;
            }
        }
        else if (imageCache[foundIdx].data != currentData)
        {
            NVGXMtexture* nvgTex = nvgxmImageHandle(vg, imageCache[foundIdx].imageId);
            if (nvgTex)
            {
                nvgTex->tex               = *gxmTex;
                nvgTex->data              = (uint8_t*)currentData;
                imageCache[foundIdx].data = currentData;
            }
            useImageId = imageCache[foundIdx].imageId;
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
            }
            else
            {
                nvgDeleteImage(vg, imageCache[0].imageId);
                imageCache[0].tex     = tex;
                imageCache[0].imageId = newId;
                imageCache[0].width   = (int)texW;
                imageCache[0].height  = (int)texH;
                imageCache[0].data    = currentData;
                imageCache[0].format  = currentFmt;
                useImageId            = newId;
            }
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

    float drawX = static_cast<float>(ox);
    float drawY = static_cast<float>(oy);
    adjustPresentation(drawY);

    const int keyboardHeight = keyboardViewportHeight.load(std::memory_order_acquire);
    if (keyboardHeight > 0)
    {
        nvgSave(vg);
        nvgScissor(vg, 0.0f, 0.0f, viewportW, static_cast<float>(keyboardHeight));
    }

    NVGpaint paint = nvgImagePattern(vg, drawX, drawY, (float)dw, (float)dh, 0.0f, useImageId, alpha);
    nvgBeginPath(vg);
    nvgRect(vg, drawX, drawY, (float)dw, (float)dh);
    nvgFillPaint(vg, paint);
    nvgFill(vg);

    if (keyboardHeight > 0)
    {
        nvgRestore(vg);
    }

    onFramePresented(currentData, frame.generation, frame.publish_timestamp_us);
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
extern "C" void ffmpeg_mark_direct_buffer_presented(const void* data);
extern "C" void ffmpeg_video_watchdog_tick(void);

void VitaVideoRenderer::onFramePresented(const void* textureData, uint32_t generation, uint32_t publishTimestampUs)
{
    bool uniqueFrame = video_frame_mark_presented(generation);
    if (uniqueFrame)
    {
        video_stats::increment(g_stats.frames_presented);
        s_presentWindowFrames++;
        if (publishTimestampUs > 0)
        {
            uint32_t diffUs = sceKernelGetProcessTimeLow() - publishTimestampUs;
            s_publish_to_render_sum_us += diffUs;
            if (diffUs < s_publish_to_render_min_us)
                s_publish_to_render_min_us = diffUs;
            if (diffUs > s_publish_to_render_max_us)
                s_publish_to_render_max_us = diffUs;
            s_publish_to_render_count++;
        }
    }
    else
    {
        video_stats::increment(g_stats.frames_repeated);
        s_presentWindowRepeats++;
    }

    // Only process ffmpeg deferred releases when in ffmpeg mode.
    // In legacy mode, the deferred list is always empty but we'd still
    // pay for a mutex lock+unlock on every single frame for nothing.
    if (video_runtime::renderMode() == 1)
    {
        ffmpeg_video_watchdog_tick();
        ffmpeg_mark_direct_buffer_presented(textureData);
        ffmpeg_increment_presented_frames();
        ffmpeg_process_deferred_releases();
    }
    uint64_t now = vita_monotonic_ms();
    if (stats_start_ms == 0)
    {
        stats_start_ms = now;
    }

    if (stats_start_ms != 0 && stats_start_ms != s_cachedStatsStart)
    {
        s_cachedStatsStart     = stats_start_ms;
        s_presentWindowFrames  = 0;
        s_presentWindowRepeats = 0;
        last_fps_window_ms     = stats_start_ms;
        // Reset decoding window counter in sync with the present window for easier comparison
        s_prevDecodedCount = video_stats::load(g_stats.frames_decoded);
    }

    if (last_fps_window_ms == 0)
    {
        last_fps_window_ms = now;
    }

    uint64_t elapsed = now - last_fps_window_ms;
    if (elapsed >= 1000)
    {
        if (elapsed == 0)
            elapsed = 1;
        video_stats::store(g_stats.current_fps,
            (uint32_t)((uint64_t)s_presentWindowFrames * 1000ULL / elapsed));
        // Compute decode FPS using the global decoded frames counter and the same time window
        uint32_t nowDecCount = video_stats::load(g_stats.frames_decoded);
        uint32_t decInWindow = nowDecCount - s_prevDecodedCount;
        video_stats::store(g_stats.decoded_fps,
            (uint32_t)((uint64_t)decInWindow * 1000ULL / elapsed));
        s_prevDecodedCount = nowDecCount;

        const uint32_t decodeCount = video_stats::exchange(g_decode_count, 0);
        if (decodeCount > 0)
        {
            const uint32_t decodeSum = video_stats::exchange(g_decode_sum_ms, 0);
            const uint32_t decodeMin = video_stats::exchange(g_decode_min_ms, 999999);
            const uint32_t decodeMax = video_stats::exchange(g_decode_max_ms, 0);
            uint32_t avg             = decodeSum / decodeCount;
            uint32_t estRtt          = 0;
            uint32_t estRttVar       = 0;
            if (connection::getEstimatedRtt(estRtt, estRttVar))
            {
                // Log warning if RTT variance is high or FPS is low
                if (estRttVar > 5 || decodeCount < 55)
                {
                    VITA_DEBUG_LOG("[PERF][DECODE] WARNING: Latency (min/avg/max): %u/%u/%u ms (frames in window: %u) - RTT: %u ms (var: %u)",
                        decodeMin, avg, decodeMax, decodeCount, estRtt, estRttVar);
                }
                else
                {
                    VITA_DEBUG_LOG("[PERF][DECODE] Latency (min/avg/max): %u/%u/%u ms (frames in window: %u) - RTT: %u ms (var: %u)",
                        decodeMin, avg, decodeMax, decodeCount, estRtt, estRttVar);
                }
            }
            else
            {
                VITA_DEBUG_LOG("[PERF][DECODE] Latency (min/avg/max): %u/%u/%u ms (frames in window: %u)",
                    decodeMin, avg, decodeMax, decodeCount);
            }
        }

        if (s_publish_to_render_count > 0)
        {
            uint32_t latencyAvgUs = s_publish_to_render_sum_us / s_publish_to_render_count;
            uint32_t latencyMinUs = s_publish_to_render_min_us == 999999 ? 0 : s_publish_to_render_min_us;
            uint32_t latencyMaxUs = s_publish_to_render_max_us;
            VITA_DEBUG_LOG("[PERF][RENDER] Publish-to-render (min/avg/max): %.2f/%.2f/%.2f ms (unique: %u repeats: %u)",
                (float)latencyMinUs / 1000.0f, (float)latencyAvgUs / 1000.0f, (float)latencyMaxUs / 1000.0f,
                s_publish_to_render_count, s_presentWindowRepeats);
            s_publish_to_render_sum_us = 0;
            s_publish_to_render_min_us = 999999;
            s_publish_to_render_max_us = 0;
            s_publish_to_render_count  = 0;
        }

        last_fps_window_ms     = now;
        s_presentWindowFrames  = 0;
        s_presentWindowRepeats = 0;
    }
}
