// VitaVideoRenderer.cpp - Unified implementation (shared + NVG + vita2d)
#include "VitaVideoRenderer.hpp"

#include <borealis/extern/nanovg/nanovg.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <borealis/core/application.hpp>

#include "legacy/modules/vita_globals.hpp"

// Override display queue depth BEFORE including GXM headers that define it.
// This reduces displayQueueMaxPendingCount from 2 to 1, saving ~16.6ms.
#define MAX_PENDING_SWAPS 1
#include <borealis/extern/nanovg/nanovg_gxm.h>
#include <stdlib.h>

#include <mutex>

namespace
{
uint32_t s_presentWindowFrames = 0;
uint64_t s_cachedStatsStart    = 0;
uint32_t s_prevDecodedCount    = 0;

bool is_gpu_yuv_experimental_enabled()
{
    extern bool g_gpu_yuv_experimental_enabled;
    return g_gpu_yuv_experimental_enabled;
}

bool is_yuv_gxm_format(uint32_t fmt)
{
    return fmt == (uint32_t)SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0 || fmt == (uint32_t)SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0;
}

uint32_t s_e2e_sum_us = 0;
uint32_t s_e2e_min_us = 999999;
uint32_t s_e2e_max_us = 0;
uint32_t s_e2e_count  = 0;
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
    if (g_stats.frames_decoded == 0)
        return;

    int displayIdx = 0;
    if (!single_frame_buffer)
    {
        int bufferMode = g_video_settings_snapshot.buffer_mode;
        if (bufferMode == 2)
        {
            // Triple-buffering: Atomic Exchange
            int current_display = __atomic_load_n(&frame_display_idx, __ATOMIC_ACQUIRE);
            if (__atomic_load_n(&frame_ready_flag, __ATOMIC_ACQUIRE))
            {
                int old_ready   = __atomic_exchange_n(&frame_ready_idx, current_display, __ATOMIC_SEQ_CST);
                current_display = old_ready;
                __atomic_store_n(&frame_display_idx, current_display, __ATOMIC_RELEASE);
                __atomic_store_n(&frame_ready_flag, false, __ATOMIC_RELEASE);
            }
            displayIdx = current_display;

            // Triple-buffering collision check: ensure display, write, and ready are all distinct
            int current_write = __atomic_load_n(&frame_write_idx, __ATOMIC_ACQUIRE);
            int current_ready = __atomic_load_n(&frame_ready_idx, __ATOMIC_ACQUIRE);
            if (displayIdx == current_write || displayIdx == current_ready || current_write == current_ready)
            {
                static uint32_t collision_counter = 0;
                if ((collision_counter++ % 181) == 0)
                {
                    VITA_DEBUG_LOG("[Video][WARN][DIAG] COLLISION DETECTED in Triple Buffer: display=%d write=%d ready=%d",
                        displayIdx, current_write, current_ready);
                }
            }
        }
        else
        {
            // Double-buffering rotation/swap
            if (__atomic_load_n(&frame_ready_flag, __ATOMIC_ACQUIRE))
            {
                int ready = __atomic_load_n(&frame_ready_idx, __ATOMIC_ACQUIRE);
                __atomic_store_n(&frame_display_idx, ready, __ATOMIC_RELEASE);
                __atomic_store_n(&frame_ready_flag, false, __ATOMIC_RELEASE);
            }
            displayIdx = __atomic_load_n(&frame_display_idx, __ATOMIC_ACQUIRE);

            // Double-buffering collision check
            int current_write = __atomic_load_n(&frame_write_idx, __ATOMIC_ACQUIRE);
            if (displayIdx == current_write)
            {
                static uint32_t collision_counter = 0;
                if ((collision_counter++ % 181) == 0)
                {
                    VITA_DEBUG_LOG("[Video][WARN][DIAG] COLLISION DETECTED in Double Buffer: display=%d write=%d",
                        displayIdx, current_write);
                }
            }
        }
    }
    else
    {
        // Single buffering loads the latest active decoded texture index atomically
        displayIdx = __atomic_load_n(&frame_display_idx, __ATOMIC_ACQUIRE);
    }

    const GxmTexture* tex = frame_textures[displayIdx];
    if (!tex)
        return;

    // Only measure E2E latency for genuinely NEW frames.
    // Without this guard, redrawing the same (stale) frame would keep
    // accumulating ever-growing deltas, reporting false multi-second latency.
    static uint32_t s_last_measured_ts = 0;
    uint32_t publishTimeUs             = __atomic_load_n(&frame_publish_timestamp_us, __ATOMIC_ACQUIRE);
    if (publishTimeUs > 0 && publishTimeUs != s_last_measured_ts)
    {
        s_last_measured_ts = publishTimeUs;
        uint32_t nowUs     = sceKernelGetProcessTimeLow();
        if (nowUs >= publishTimeUs)
        {
            uint32_t diffUs = nowUs - publishTimeUs;
            s_e2e_sum_us += diffUs;
            if (diffUs < s_e2e_min_us)
                s_e2e_min_us = diffUs;
            if (diffUs > s_e2e_max_us)
                s_e2e_max_us = diffUs;
            s_e2e_count++;
        }
    }

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

    // Use image caching for better performance (reduces micro-stuttering)
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
                imageCache[foundIdx].imageId = newId;
                imageCache[foundIdx].width   = (int)texW;
                imageCache[foundIdx].height  = (int)texH;
                imageCache[foundIdx].data    = currentData;
                imageCache[foundIdx].format  = currentFmt;
                useImageId                   = newId;
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

    // Draw complete frame at once
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
    // Only process ffmpeg deferred releases when in ffmpeg mode.
    // In legacy mode, the deferred list is always empty but we'd still
    // pay for a mutex lock+unlock on every single frame for nothing.
    if (g_video_settings_snapshot.render_mode == 1)
    {
        ffmpeg_process_deferred_releases();
        ffmpeg_increment_presented_frames();
    }
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
        uint32_t nowDecCount = g_stats.frames_decoded;
        uint32_t decInWindow = nowDecCount - s_prevDecodedCount;
        g_stats.decoded_fps  = (uint32_t)((uint64_t)decInWindow * 1000ULL / elapsed);
        s_prevDecodedCount   = nowDecCount;

        if (g_decode_count > 0)
        {
            uint32_t avg       = g_decode_sum_ms / g_decode_count;
            uint32_t estRtt    = 0;
            uint32_t estRttVar = 0;
            if (!g_session_stopping && LiGetEstimatedRttInfo(&estRtt, &estRttVar))
            {
                // Log warning if RTT variance is high or FPS is low
                if (estRttVar > 5 || g_decode_count < 55)
                {
                    VITA_DEBUG_LOG("[PERF][DECODE] WARNING: Latency (min/avg/max): %u/%u/%u ms (frames in window: %u) - RTT: %u ms (var: %u)",
                        g_decode_min_ms, avg, g_decode_max_ms, g_decode_count, estRtt, estRttVar);
                }
                else
                {
                    VITA_DEBUG_LOG("[PERF][DECODE] Latency (min/avg/max): %u/%u/%u ms (frames in window: %u) - RTT: %u ms (var: %u)",
                        g_decode_min_ms, avg, g_decode_max_ms, g_decode_count, estRtt, estRttVar);
                }
            }
            else
            {
                VITA_DEBUG_LOG("[PERF][DECODE] Latency (min/avg/max): %u/%u/%u ms (frames in window: %u)",
                    g_decode_min_ms, avg, g_decode_max_ms, g_decode_count);
            }
            g_decode_min_ms = 999999;
            g_decode_max_ms = 0;
            g_decode_sum_ms = 0;
            g_decode_count  = 0;
        }

        if (s_e2e_count > 0)
        {
            uint32_t e2eAvgUs = s_e2e_sum_us / s_e2e_count;
            uint32_t e2eMinUs = s_e2e_min_us == 999999 ? 0 : s_e2e_min_us;
            uint32_t e2eMaxUs = s_e2e_max_us;
            VITA_DEBUG_LOG("[PERF][RENDER] End-to-End Latency (min/avg/max): %.2f/%.2f/%.2f ms (frames: %u)",
                (float)e2eMinUs / 1000.0f, (float)e2eAvgUs / 1000.0f, (float)e2eMaxUs / 1000.0f, s_e2e_count);
            s_e2e_sum_us = 0;
            s_e2e_min_us = 999999;
            s_e2e_max_us = 0;
            s_e2e_count  = 0;
        }

        last_fps_window_ms    = now;
        s_presentWindowFrames = 0;
    }
}
