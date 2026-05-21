#include "ffmpeg.hpp"

#include <Limelight.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <borealis/core/logger.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "gamestream/client.h"
#include "gamestream/errors.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <borealis/core/application.hpp>

#include "legacy/modules/vita_globals.hpp"
#include "network/NetworkOptimizations.hpp"
#include "session/vita_session.hpp"
#include "video/VideoFrameHolder.hpp"
#include "video/VitaVideoRenderer.hpp"

#ifdef BOREALIS_USE_GXM
#include <borealis/extern/nanovg/nanovg_gxm_utils.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#endif

static FFmpegVideoContext* g_ffmpeg_context = nullptr;
static std::mutex g_ffmpeg_mutex;
static std::atomic<int> g_active_decodes { 0 };
static std::atomic<bool> g_ffmpeg_stop_request { false };
static uint32_t g_ffmpeg_submit_counter = 0;
static unsigned g_ffmpeg_frame_index    = 0;

static inline void wait_for_borealis_gxm_idle()
{
#ifdef BOREALIS_USE_GXM
    NVGXMwindow* win = gxmGetWindow();
    if (win && win->context)
    {
        sceKernelDelayThread(1000);
    }
#endif
}

static bool is_gpu_yuv_experimental_enabled()
{
    extern bool g_gpu_yuv_experimental_enabled;
    return g_gpu_yuv_experimental_enabled;
}

struct ffmpeg_perf_counters
{
    uint32_t window_start_ms;
    uint32_t submit_calls;
    uint32_t decoded_frames;
    uint32_t published_frames;
    uint32_t sws_calls;
    uint64_t sws_total_us;
    uint32_t sws_max_us;
    uint64_t submit_total_us;
    uint32_t submit_max_us;
    uint64_t lock_wait_total_us;
    uint32_t lock_wait_max_us;
    uint64_t drain_total_us;
    uint32_t drain_max_us;
    uint64_t copy_total_us;
    uint32_t copy_max_us;
    uint64_t send_total_us;
    uint32_t send_max_us;
    uint64_t recv_total_us;
    uint32_t recv_max_us;
    uint32_t dropped_decode_units;
    uint32_t dropped_stale_frames;
    uint32_t adaptive_drop_interval;
};

static ffmpeg_perf_counters g_perf = { 0 };

static inline uint32_t perf_now_us()
{
    return sceKernelGetProcessTimeLow();
}

static void perf_report_if_due()
{
    uint32_t nowMs = (uint32_t)vita_monotonic_ms();
    if (g_perf.window_start_ms == 0)
    {
        g_perf.window_start_ms = nowMs;
        return;
    }

    uint32_t elapsedMs = nowMs - g_perf.window_start_ms;
    if (elapsedMs < 1000)
    {
        return;
    }

    uint32_t denom        = elapsedMs ? elapsedMs : 1;
    uint32_t submitFps    = (uint32_t)(((uint64_t)g_perf.submit_calls * 1000ULL) / denom);
    uint32_t decodedFps   = (uint32_t)(((uint64_t)g_perf.decoded_frames * 1000ULL) / denom);
    uint32_t publishedFps = (uint32_t)(((uint64_t)g_perf.published_frames * 1000ULL) / denom);
    uint32_t swsAvgUs     = g_perf.sws_calls ? (uint32_t)(g_perf.sws_total_us / g_perf.sws_calls) : 0;
    uint32_t submitAvgUs  = g_perf.submit_calls ? (uint32_t)(g_perf.submit_total_us / g_perf.submit_calls) : 0;
    uint32_t lockAvgUs    = g_perf.submit_calls ? (uint32_t)(g_perf.lock_wait_total_us / g_perf.submit_calls) : 0;
    uint32_t drainAvgUs   = g_perf.submit_calls ? (uint32_t)(g_perf.drain_total_us / g_perf.submit_calls) : 0;
    uint32_t copyAvgUs    = g_perf.submit_calls ? (uint32_t)(g_perf.copy_total_us / g_perf.submit_calls) : 0;
    uint32_t sendAvgUs    = g_perf.submit_calls ? (uint32_t)(g_perf.send_total_us / g_perf.submit_calls) : 0;
    uint32_t recvAvgUs    = g_perf.submit_calls ? (uint32_t)(g_perf.recv_total_us / g_perf.submit_calls) : 0;

    VITA_DEBUG_LOG("[PERF][VIDEO] window_ms=%u submit_fps=%u decoded_fps=%u presented_fps=%u submit_calls=%u decoded=%u published=%u sws_calls=%u sws_avg_us=%u sws_max_us=%u submit_avg_us=%u submit_max_us=%u lock_avg_us=%u lock_max_us=%u drain_avg_us=%u drain_max_us=%u copy_avg_us=%u copy_max_us=%u send_avg_us=%u send_max_us=%u recv_avg_us=%u recv_max_us=%u drop_units=%u drop_stale=%u drop_every=%u",
        elapsedMs,
        submitFps,
        decodedFps,
        g_stats.current_fps,
        g_perf.submit_calls,
        g_perf.decoded_frames,
        g_perf.published_frames,
        g_perf.sws_calls,
        swsAvgUs,
        g_perf.sws_max_us,
        submitAvgUs,
        g_perf.submit_max_us,
        lockAvgUs,
        g_perf.lock_wait_max_us,
        drainAvgUs,
        g_perf.drain_max_us,
        copyAvgUs,
        g_perf.copy_max_us,
        sendAvgUs,
        g_perf.send_max_us,
        recvAvgUs,
        g_perf.recv_max_us,
        g_perf.dropped_decode_units,
        g_perf.dropped_stale_frames,
        g_perf.adaptive_drop_interval);

    g_perf.window_start_ms      = nowMs;
    g_perf.submit_calls         = 0;
    g_perf.decoded_frames       = 0;
    g_perf.published_frames     = 0;
    g_perf.sws_calls            = 0;
    g_perf.sws_total_us         = 0;
    g_perf.sws_max_us           = 0;
    g_perf.submit_total_us      = 0;
    g_perf.submit_max_us        = 0;
    g_perf.lock_wait_total_us   = 0;
    g_perf.lock_wait_max_us     = 0;
    g_perf.drain_total_us       = 0;
    g_perf.drain_max_us         = 0;
    g_perf.copy_total_us        = 0;
    g_perf.copy_max_us          = 0;
    g_perf.send_total_us        = 0;
    g_perf.send_max_us          = 0;
    g_perf.recv_total_us        = 0;
    g_perf.recv_max_us          = 0;
    g_perf.dropped_decode_units = 0;
    g_perf.dropped_stale_frames = 0;
}

#ifdef BOREALIS_USE_GXM
struct dr_format_spec
{
    enum AVPixelFormat ff_format;
    SceGxmTextureFormat sce_format;
    uint32_t alignment_pitch;
};

static const dr_format_spec g_dr_format_spec_list[] = {
    { AV_PIX_FMT_RGBA, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 16 },
    { AV_PIX_FMT_BGR565LE, SCE_GXM_TEXTURE_FORMAT_U5U6U5_BGR, 16 },
    { AV_PIX_FMT_BGR555LE, SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR, 16 },
    { AV_PIX_FMT_YUV420P, SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0, 32 },
    { AV_PIX_FMT_NV12, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0, 16 },
    { AV_PIX_FMT_VITA_NV12, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0, 16 },
    { AV_PIX_FMT_VITA_YUV420P, SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0, 32 },
};

static const dr_format_spec* get_dr_format_spec(enum AVPixelFormat fmt)
{
    for (unsigned i = 0; i < sizeof(g_dr_format_spec_list) / sizeof(g_dr_format_spec_list[0]); ++i)
    {
        if (g_dr_format_spec_list[i].ff_format == fmt)
        {
            return &g_dr_format_spec_list[i];
        }
    }
    return nullptr;
}

static std::mutex g_dr_mutex;
// Deferred direct rendering texture unmap and free queue to avoid GPU page faults
struct DeferredDirectTextureRelease
{
    void* vram_data;
    SceUID memblock;
    uint32_t frame_index;
};
static std::vector<DeferredDirectTextureRelease> g_deferred_releases;
static std::mutex g_deferred_releases_mutex;

static std::atomic<uint32_t> g_ffmpeg_presented_frames { 0 };

extern "C" void ffmpeg_increment_presented_frames(void)
{
    g_ffmpeg_presented_frames.fetch_add(1, std::memory_order_release);
}

static inline void process_pending_vram_frees_if_safe() { }

static void dummy_vram_free(void* opaque, uint8_t* data)
{
    // CDRAM buffers and mappings are managed dynamically by g_deferred_releases
    // to prevent unmapping/freeing memory while the GPU is still sampling it.
    SceUID mb = (SceUID)(intptr_t)opaque;
    if (mb > 0)
    {
        uint32_t current_presented = g_ffmpeg_presented_frames.load(std::memory_order_acquire);
        std::lock_guard<std::mutex> lock_releases(g_deferred_releases_mutex);
        g_deferred_releases.push_back({ data, mb, current_presented });
    }
}

extern "C" void ffmpeg_process_deferred_releases(void)
{
    std::vector<DeferredDirectTextureRelease> to_process;
    {
        std::lock_guard<std::mutex> lock(g_deferred_releases_mutex);
        if (g_deferred_releases.empty())
        {
            return;
        }
        uint32_t current_presented = g_ffmpeg_presented_frames.load(std::memory_order_acquire);
        auto it                    = g_deferred_releases.begin();
        while (it != g_deferred_releases.end())
        {
            // Grace window: wait for 3 presented frames to ensure GXM TBDR scene is fully completed and presented
            if (current_presented >= it->frame_index + 3)
            {
                to_process.push_back(*it);
                it = g_deferred_releases.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (const auto& item : to_process)
    {
        if (item.vram_data)
        {
            sceGxmUnmapMemory(item.vram_data);
        }
        if (item.memblock > 0)
        {
            sceKernelFreeMemBlock(item.memblock);
            // VITA_DEBUG_LOG("[FFMPEG][DEFER] Safely unmapped and freed memblock %d on render thread (grace window elapsed, presented=%u, queued_at=%u)",
            //                item.memblock, g_ffmpeg_presented_frames.load(std::memory_order_acquire), item.frame_index);
        }
    }
}

extern "C" void ffmpeg_flush_deferred_releases(void)
{
    std::vector<DeferredDirectTextureRelease> to_process;
    {
        std::lock_guard<std::mutex> lock(g_deferred_releases_mutex);
        if (g_deferred_releases.empty())
        {
            return;
        }
        to_process = std::move(g_deferred_releases);
        g_deferred_releases.clear();
    }
    for (const auto& item : to_process)
    {
        if (item.vram_data)
        {
            sceGxmUnmapMemory(item.vram_data);
        }
        if (item.memblock > 0)
        {
            sceKernelFreeMemBlock(item.memblock);
            VITA_DEBUG_LOG("[FFMPEG][DEFER] Flushed and freed memblock %d immediately on stream stop", item.memblock);
        }
    }
}

static bool vram_alloc(int* size, SceUID* mb, void** ptr)
{
    *size        = FFALIGN(*size, 256 * 1024);
    SceUID block = sceKernelAllocMemBlock("ffmpeg_gpu_mem",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
        *size,
        nullptr);
    if (block < 0)
    {
        return false;
    }

    void* base = nullptr;
    if (sceKernelGetMemBlockBase(block, &base) != 0)
    {
        sceKernelFreeMemBlock(block);
        return false;
    }

    *mb  = block;
    *ptr = base;
    return true;
}

extern "C" int get_buffer2_direct(AVCodecContext* avctx, AVFrame* pic, int /*flags*/)
{
    const dr_format_spec* spec = get_dr_format_spec((enum AVPixelFormat)pic->format);
    if (!spec)
    {
        return AVERROR(EINVAL);
    }

    int width  = FFMAX(FFALIGN(pic->width, 16), 64);
    int height = FFMAX(FFALIGN(pic->height, 16), 64);
    int pitch  = FFALIGN(width, (int)spec->alignment_pitch);

    SceUID mb  = 0;
    void* vram = nullptr;
    int size   = av_image_get_buffer_size((enum AVPixelFormat)pic->format, pitch, height, 1);
    if (!vram_alloc(&size, &mb, &vram))
    {
        return AVERROR(ENOMEM);
    }

    int mapRes = sceGxmMapMemory(vram, size, SCE_GXM_MEMORY_ATTRIB_READ);
    if (mapRes < 0)
    {
        sceKernelFreeMemBlock(mb);
        return AVERROR(ENOMEM);
    }

    pic->buf[0] = av_buffer_create((uint8_t*)vram, size, dummy_vram_free, (void*)(intptr_t)mb, 0);
    if (!pic->buf[0])
    {
        sceGxmUnmapMemory(vram);
        sceKernelFreeMemBlock(mb);
        return AVERROR(ENOMEM);
    }

    av_image_fill_arrays(pic->data,
        pic->linesize,
        (const uint8_t*)vram,
        (enum AVPixelFormat)pic->format,
        pitch,
        height,
        1);
    return 0;
}

struct dr_texture
{
    GxmTexture impl;
    AVFrame frame;
    bool vram_mapped;
};

static dr_texture* dr_texture_alloc()
{
    dr_texture* tex = (dr_texture*)malloc(sizeof(dr_texture));
    if (!tex)
    {
        return nullptr;
    }
    memset(tex, 0, sizeof(*tex));
    tex->impl.mem_uid = -1;
    av_frame_unref(&tex->frame);
    tex->vram_mapped = false;
    return tex;
}

static void dr_texture_detach(dr_texture* tex)
{
    if (!tex)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_dr_mutex);
    tex->vram_mapped = false;
    av_frame_unref(&tex->frame);
}

static void dr_texture_free(dr_texture** p_tex)
{
    if (!p_tex || !*p_tex)
    {
        return;
    }
    dr_texture_detach(*p_tex);
    free(*p_tex);
    *p_tex = nullptr;
}

static void dr_texture_attach(dr_texture* tex, AVFrame* frame)
{
    if (!tex || !frame)
    {
        return;
    }

    const dr_format_spec* spec = get_dr_format_spec((enum AVPixelFormat)frame->format);
    if (!spec)
    {
        return;
    }

    AVBufferRef* buf = frame->buf[0];
    if (!buf)
    {
        return;
    }

    int width  = FFMAX(FFALIGN(frame->width, 16), 64);
    int height = FFMAX(FFALIGN(frame->height, 16), 64);

    std::lock_guard<std::mutex> lock(g_dr_mutex);
    sceGxmTextureInitLinear(&tex->impl.gxm_tex, buf->data, spec->sce_format, width, height, 0);
    tex->impl.width     = (uint32_t)width;
    tex->impl.height    = (uint32_t)height;
    tex->impl.stride    = (uint32_t)frame->linesize[0];
    tex->impl.data_size = (uint32_t)buf->size;
    av_frame_unref(&tex->frame);
    // Keep a reference to the frame for DR texture without transferring ownership
    // from the decoder. This avoids freeing the underlying memblock while the
    // decoder still has an active ref and prevents use-after-free in the
    // codec's internal threads (e.g., loop_filter).
    if (av_frame_ref(&tex->frame, frame) < 0)
    {
        // Leave tex->frame cleared; decoder still owns the frame
        return;
    }
}
#endif // BOREALIS_USE_GXM

static void reset_global_slots()
{
    std::lock_guard<std::mutex> slotLock(g_frame_slots_mutex);
    frame_textures[0]   = nullptr;
    frame_textures[1]   = nullptr;
    frame_textures[2]   = nullptr;
    frame_display_idx   = 0;
    frame_ready_idx     = 0;
    frame_write_idx     = 0;
    single_frame_buffer = true;
}

static void free_decode_unit(PDECODE_UNIT unit)
{
    if (!unit)
    {
        return;
    }
    PLENTRY entry = unit->bufferList;
    while (entry)
    {
        PLENTRY next = entry->next;
        if (entry->data)
        {
            free(entry->data);
        }
        free(entry);
        entry = next;
    }
    free(unit);
}

static uint64_t monotonic_ms_local()
{
    return vita_monotonic_ms();
}

// Experimental YUV CSC fast-path is currently unstable with the active render path.
// Keep disabled by default until we have a dedicated YUV-safe presentation path.
// Runtime gated experimental path to bypass CPU sws conversion and upload YUV
// directly to CSC textures for GPU-side conversion/sampling.
static bool k_enable_yuv_csc_fast_path()
{
    return is_gpu_yuv_experimental_enabled();
}

static int format_to_sw_texture_format(enum AVPixelFormat srcFmt)
{
#ifdef BOREALIS_USE_GXM
    // GXM CSC avoids the CPU sws_scale conversion for YUV/NV12 frames.
    if (srcFmt == AV_PIX_FMT_YUV420P || srcFmt == AV_PIX_FMT_VITA_YUV420P)
    {
        return (int)SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0;
    }
    if (srcFmt == AV_PIX_FMT_NV12 || srcFmt == AV_PIX_FMT_VITA_NV12)
    {
        return (int)SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0;
    }
#else
    (void)srcFmt;
#endif
    return (int)SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR;
}

static int ensure_sw_texture(FFmpegVideoContext* ctx, int width, int height, enum AVPixelFormat srcFmt)
{
#ifndef BOREALIS_USE_GXM
    (void)ctx;
    (void)width;
    (void)height;
    (void)srcFmt;
    return -1;
#else
    if (width <= 0 || height <= 0)
    {
        return -1;
    }

    int textureFormat = format_to_sw_texture_format(srcFmt);

    if (ctx->sw_textures[0] && ctx->sw_textures[1] && ctx->sw_textures[2] && ctx->sw_texture_width == width && ctx->sw_texture_height == height && ctx->sw_texture_format == textureFormat)
    {
        return 0;
    }

    for (int i = 0; i < 3; ++i)
    {
        if (ctx->sw_textures[i])
        {
            gxm_texture_free(ctx->sw_textures[i]);
            ctx->sw_textures[i] = nullptr;
        }
    }
    ctx->sw_texture = nullptr;

    for (int i = 0; i < 3; ++i)
    {
        ctx->sw_textures[i] = gxm_texture_create(width, height, (SceGxmTextureFormat)textureFormat, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW);
        if (!ctx->sw_textures[i])
        {
            for (int j = 0; j <= i; ++j)
            {
                if (ctx->sw_textures[j])
                {
                    gxm_texture_free(ctx->sw_textures[j]);
                    ctx->sw_textures[j] = nullptr;
                }
            }
            return -1;
        }

        void* initDst = gxm_texture_get_datap(ctx->sw_textures[i]);
        if (initDst)
        {
            // For YUV CSC textures on Vita, buffer layout/size can be driver-specific.
            // Clearing with a computed size may overrun and crash; only clear RGBA textures.
            if (textureFormat == (int)SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR)
            {
                int initStride = gxm_texture_get_stride(ctx->sw_textures[i]);
                memset(initDst, 0, (size_t)initStride * (size_t)height);
            }
        }
    }

    ctx->sw_texture_width    = width;
    ctx->sw_texture_height   = height;
    ctx->sw_texture_format   = textureFormat;
    ctx->sw_write_idx        = 0;
    ctx->sw_last_present_idx = 2;
    ctx->sw_texture          = ctx->sw_textures[ctx->sw_last_present_idx];
    ctx->sw_texture_stride   = gxm_texture_get_stride(ctx->sw_textures[ctx->sw_write_idx]);
    return 0;
#endif
}

#ifdef BOREALIS_USE_GXM
static void rotate_sw_ring_and_publish(FFmpegVideoContext* ctx, GxmTexture* writeTex, int width, int height, int stride)
{
    ctx->sw_last_present_idx = ctx->sw_write_idx;
    ctx->sw_write_idx        = (ctx->sw_write_idx + 1) % 3;
    if (ctx->sw_write_idx == ctx->sw_last_present_idx)
    {
        ctx->sw_write_idx = (ctx->sw_write_idx + 1) % 3;
    }
    ctx->sw_texture        = ctx->sw_textures[ctx->sw_last_present_idx];
    ctx->sw_texture_stride = stride;

    ctx->current_frame.texture       = writeTex;
    ctx->current_frame.width         = width;
    ctx->current_frame.height        = height;
    ctx->current_frame.has_frame     = true;
    ctx->current_frame.direct_memory = false;
    ctx->using_direct_memory         = false;
}

static bool copy_yuv420p_to_csc_texture(GxmTexture* writeTex, AVFrame* frame, int* outYStride)
{
#ifdef BOREALIS_USE_GXM
    if (!writeTex || !frame || !frame->data[0] || !frame->data[1] || !frame->data[2])
    {
        return false;
    }

    uint8_t* dst = (uint8_t*)gxm_texture_get_datap(writeTex);
    if (!dst)
    {
        return false;
    }

    int w    = frame->width;
    int h    = frame->height;
    int texW = (int)sceGxmTextureGetWidth(&writeTex->gxm_tex);
    int texH = (int)sceGxmTextureGetHeight(&writeTex->gxm_tex);
    // For linear YUV420P3 on GXM, use planar pitch (bytes per luma/chroma sample),
    // not vita2d RGBA byte stride helper.
    int yStrideDst  = (texW + 7) & ~7;
    int uvStrideDst = yStrideDst / 2;
    if (w <= 0 || h <= 0 || texW <= 0 || texH <= 0 || w > texW || h > texH || yStrideDst < w || uvStrideDst < (w / 2))
    {
        return false;
    }

    uint8_t* dstY = dst;
    uint8_t* dstU = dstY + (size_t)yStrideDst * (size_t)texH;
    uint8_t* dstV = dstU + (size_t)uvStrideDst * (size_t)(texH / 2);

    if (frame->linesize[0] == yStrideDst)
    {
        memcpy(dstY, frame->data[0], (size_t)yStrideDst * (size_t)h);
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            memcpy(dstY + (size_t)y * (size_t)yStrideDst,
                frame->data[0] + (size_t)y * (size_t)frame->linesize[0],
                (size_t)w);
        }
    }

    if (frame->linesize[1] == uvStrideDst && frame->linesize[2] == uvStrideDst)
    {
        memcpy(dstU, frame->data[1], (size_t)uvStrideDst * (size_t)(h / 2));
        memcpy(dstV, frame->data[2], (size_t)uvStrideDst * (size_t)(h / 2));
    }
    else
    {
        for (int y = 0; y < (h / 2); ++y)
        {
            memcpy(dstU + (size_t)y * (size_t)uvStrideDst,
                frame->data[1] + (size_t)y * (size_t)frame->linesize[1],
                (size_t)(w / 2));
            memcpy(dstV + (size_t)y * (size_t)uvStrideDst,
                frame->data[2] + (size_t)y * (size_t)frame->linesize[2],
                (size_t)(w / 2));
        }
    }

    if (outYStride)
    {
        *outYStride = yStrideDst;
    }
    return true;
#else
    (void)writeTex;
    (void)frame;
    (void)outYStride;
    return false;
#endif
}

static bool copy_nv12_to_csc_texture(GxmTexture* writeTex, AVFrame* frame, int* outYStride)
{
#ifdef BOREALIS_USE_GXM
    if (!writeTex || !frame || !frame->data[0] || !frame->data[1])
    {
        return false;
    }

    uint8_t* dst = (uint8_t*)gxm_texture_get_datap(writeTex);
    if (!dst)
    {
        return false;
    }

    int w           = frame->width;
    int h           = frame->height;
    int texW        = (int)sceGxmTextureGetWidth(&writeTex->gxm_tex);
    int texH        = (int)sceGxmTextureGetHeight(&writeTex->gxm_tex);
    int yStrideDst  = (texW + 7) & ~7;
    int uvStrideDst = yStrideDst; // NV12: UV plane has same stride in bytes as Y plane
    if (w <= 0 || h <= 0 || texW <= 0 || texH <= 0 || w > texW || h > texH || yStrideDst < w)
    {
        return false;
    }

    uint8_t* dstY  = dst;
    uint8_t* dstUV = dstY + (size_t)yStrideDst * (size_t)texH;

    if (frame->linesize[0] == yStrideDst)
    {
        memcpy(dstY, frame->data[0], (size_t)yStrideDst * (size_t)h);
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            memcpy(dstY + (size_t)y * (size_t)yStrideDst,
                frame->data[0] + (size_t)y * (size_t)frame->linesize[0],
                (size_t)w);
        }
    }

    if (frame->linesize[1] == uvStrideDst)
    {
        memcpy(dstUV, frame->data[1], (size_t)uvStrideDst * (size_t)(h / 2));
    }
    else
    {
        for (int y = 0; y < (h / 2); ++y)
        {
            memcpy(dstUV + (size_t)y * (size_t)uvStrideDst,
                frame->data[1] + (size_t)y * (size_t)frame->linesize[1],
                (size_t)w);
        }
    }

    if (outYStride)
    {
        *outYStride = yStrideDst;
    }
    return true;
#else
    (void)writeTex;
    (void)frame;
    (void)outYStride;
    return false;
#endif
}

static bool publish_direct_frame(FFmpegVideoContext* ctx, AVFrame* frame)
{
    if (!frame || !frame->buf[0])
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_direct_frame: null frame or buf[0]");
        return false;
    }

    if (!frame->buf[0]->data)
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_direct_frame: buf->data is null");
        return false;
    }

    if (!get_dr_format_spec((enum AVPixelFormat)frame->format))
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_direct_frame: no spec for format %d", frame->format);
        return false;
    }

    // Use triple buffering in direct mode so we only recycle the oldest slot.
    // Rotation: spare -> front, front -> back, back -> spare.
    dr_texture* texFront = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_front_idx]);
    dr_texture* texSpare = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_spare_idx]);
    if (!texSpare)
    {
        texSpare = dr_texture_alloc();
        if (!texSpare)
        {
            return false;
        }
        ctx->dr_textures[ctx->dr_spare_idx] = texSpare;
    }

    // Recycle only the oldest texture slot.
    dr_texture_detach(texSpare);
    dr_texture_attach(texSpare, frame);

    int prevFront     = ctx->dr_front_idx;
    int prevBack      = ctx->dr_back_idx;
    ctx->dr_front_idx = ctx->dr_spare_idx;
    ctx->dr_back_idx  = prevFront;
    ctx->dr_spare_idx = prevBack;

    texFront                         = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_front_idx]);
    ctx->current_frame.texture       = &texFront->impl;
    ctx->current_frame.width         = frame->width;
    ctx->current_frame.height        = frame->height;
    ctx->current_frame.has_frame     = true;
    ctx->current_frame.direct_memory = true;
    ctx->using_direct_memory         = true;
    g_perf.published_frames++;
    return true;
}
#endif

static bool publish_sw_frame(FFmpegVideoContext* ctx, AVFrame* frame)
{
#ifndef BOREALIS_USE_GXM
    (void)ctx;
    (void)frame;
    return false;
#else
    static uint32_t s_sw_log_counter = 0;
    bool verboseSwLog                = ((s_sw_log_counter++ % 1200) == 0);
    if (verboseSwLog)
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: frame=%p", frame);
    }
    if (!frame)
    {
        return false;
    }

    if (!frame->data[0])
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: frame->data[0] is null");
        return false;
    }
    if (verboseSwLog)
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: frame data[0]=%p linesize[0]=%d", frame->data[0], frame->linesize[0]);
        VITA_DEBUG_LOG("[FFMPEG] frame data[1]=%p linesize[1]=%d data[2]=%p linesize[2]=%d", frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
    }

    // Use the actual source pixel format reported by the decoder/frame.
    enum AVPixelFormat srcFmt = (enum AVPixelFormat)frame->format;

    if (ensure_sw_texture(ctx, frame->width, frame->height, srcFmt) < 0)
    {
        return false;
    }

    GxmTexture* writeTex = ctx->sw_textures[ctx->sw_write_idx];
    if (!writeTex)
    {
        return false;
    }

    uint8_t* dst = (uint8_t*)gxm_texture_get_datap(writeTex);
    if (!dst)
    {
        return false;
    }
    int dstStride = gxm_texture_get_stride(writeTex);

    // YUV420P fast path: keep frame in YUV and let GXM CSC do conversion at sampling time.
    if (srcFmt == AV_PIX_FMT_YUV420P || srcFmt == AV_PIX_FMT_VITA_YUV420P)
    {
        int yStrideUsed = 0;
        if (copy_yuv420p_to_csc_texture(writeTex, frame, &yStrideUsed))
        {
            rotate_sw_ring_and_publish(ctx, writeTex, frame->width, frame->height, yStrideUsed > 0 ? yStrideUsed : dstStride);
            g_perf.published_frames++;
            if (verboseSwLog)
            {
                VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: fast YUV420P->CSC path used (y_stride=%d tex_w=%u tex_h=%u)",
                    yStrideUsed,
                    sceGxmTextureGetWidth(&writeTex->gxm_tex),
                    sceGxmTextureGetHeight(&writeTex->gxm_tex));
            }
            return true;
        }
        if (verboseSwLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: fast YUV420P path failed, fallback to sws_scale");
        }
    }

    // NV12 fast path: keep frame in NV12 (YVU420P2) and let GXM CSC do conversion at sampling time.
    if (srcFmt == AV_PIX_FMT_NV12 || srcFmt == AV_PIX_FMT_VITA_NV12)
    {
        int yStrideUsed = 0;
        if (copy_nv12_to_csc_texture(writeTex, frame, &yStrideUsed))
        {
            rotate_sw_ring_and_publish(ctx, writeTex, frame->width, frame->height, yStrideUsed > 0 ? yStrideUsed : dstStride);
            g_perf.published_frames++;
            if (verboseSwLog)
            {
                VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: fast NV12->CSC path used (y_stride=%d tex_w=%u tex_h=%u)",
                    yStrideUsed,
                    sceGxmTextureGetWidth(&writeTex->gxm_tex),
                    sceGxmTextureGetHeight(&writeTex->gxm_tex));
            }
            return true;
        }
        if (verboseSwLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: fast NV12 path failed, fallback to sws_scale");
        }
    }

    // Fast path: decoder already outputs RGBA, so just copy rows into the
    // destination texture and skip sws conversion.
    if (srcFmt == AV_PIX_FMT_RGBA && frame->data[0] && frame->linesize[0] > 0)
    {
        int copyWidthBytes = frame->width * 4;
        int srcStride      = frame->linesize[0];
        int rows           = frame->height;
        for (int y = 0; y < rows; ++y)
        {
            const uint8_t* srcRow = frame->data[0] + (size_t)y * (size_t)srcStride;
            uint8_t* dstRow       = dst + (size_t)y * (size_t)dstStride;
            memcpy(dstRow, srcRow, (size_t)copyWidthBytes);
        }

        rotate_sw_ring_and_publish(ctx, writeTex, frame->width, frame->height, dstStride);
        g_perf.published_frames++;

        if (verboseSwLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: fast RGBA path used (stride src=%d dst=%d)", srcStride, dstStride);
        }
        return true;
    }

    bool needSwsReinit = (ctx->sws_context == nullptr || ctx->sws_src_w != frame->width || ctx->sws_src_h != frame->height || ctx->sws_src_fmt != (int)srcFmt);

    if (needSwsReinit)
    {
        if (verboseSwLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] frame->format=%d, using srcFmt=%d for sws_getCachedContext (w=%d h=%d)",
                frame->format, (int)srcFmt, frame->width, frame->height);
        }
        ctx->sws_context = sws_getCachedContext(ctx->sws_context,
            frame->width,
            frame->height,
            srcFmt,
            frame->width,
            frame->height,
            AV_PIX_FMT_RGBA,
            SWS_POINT,
            nullptr,
            nullptr,
            nullptr);
        if (!ctx->sws_context)
        {
            brls::Logger::error("[FFMPEG] Unable to create swscale context for format {}", frame->format);
            return false;
        }
        ctx->sws_src_w   = frame->width;
        ctx->sws_src_h   = frame->height;
        ctx->sws_src_fmt = (int)srcFmt;
    }

    const uint8_t* inData[4] = { frame->data[0], frame->data[1], frame->data[2], frame->data[3] };
    int inStride[4]          = { frame->linesize[0], frame->linesize[1], frame->linesize[2], frame->linesize[3] };
    uint8_t* outData[4]      = { dst, nullptr, nullptr, nullptr };
    int outStride[4]         = { dstStride, 0, 0, 0 };
    int w                    = frame->width;
    int h                    = frame->height;
    uint8_t* rgba_tmp        = nullptr;
    int rgba_stride          = w * 4;
    bool used_out_tmp        = false;
    {
        uintptr_t dv = (uintptr_t)dst;
        if ((dv & 0xFF000000u) == 0x82000000u)
        {
            rgba_tmp = (uint8_t*)av_malloc((size_t)rgba_stride * (size_t)h);
            if (rgba_tmp)
            {
                used_out_tmp = true;
                outData[0]   = rgba_tmp;
                outStride[0] = rgba_stride;
            }
            else
            {
                VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: failed to alloc rgba_tmp");
            }
        }
    }
    // If the decoder produced planes in VRAM (non-CPU-accessible), sws_scale may fail.
    // Detect VRAM-backed pointers and copy to temporary packed RAM buffers if needed.
    bool used_temp               = false;
    uint8_t* tmp_y               = nullptr;
    uint8_t* tmp_u               = nullptr;
    uint8_t* tmp_v               = nullptr;
    int tmp_inStride[4]          = { inStride[0], inStride[1], inStride[2], inStride[3] };
    const uint8_t* tmp_inData[4] = { inData[0], inData[1], inData[2], inData[3] };

    auto is_vram_ptr = [](const void* p) -> bool
    {
        if (!p)
            return false;
        uintptr_t v = (uintptr_t)p;
        // Heuristic: Vita VRAM allocations commonly reside at 0x82000000+
        return (v & 0xFF000000u) == 0x82000000u;
    };

    if (is_vram_ptr(inData[0]) || is_vram_ptr(inData[1]) || is_vram_ptr(inData[2]))
    {
        // Prefer to pack the whole frame into a single contiguous CPU buffer using libav util
        // This avoids per-plane copy mistakes and handles linesize/padding correctly.
        int packed_size = av_image_get_buffer_size(srcFmt, frame->width, frame->height, 1);
        if (packed_size > 0)
        {
            uint8_t* packed_buf = (uint8_t*)av_malloc((size_t)packed_size);
            if (packed_buf)
            {
                int copy_ret = av_image_copy_to_buffer(packed_buf, packed_size, frame->data, frame->linesize, srcFmt, frame->width, frame->height, 1);
                if (copy_ret > 0)
                {
                    uint8_t* packed_planes[4] = { nullptr };
                    int packed_lines[4]       = { 0 };
                    av_image_fill_arrays(packed_planes, packed_lines, packed_buf, srcFmt, frame->width, frame->height, 1);
                    tmp_inData[0]   = packed_planes[0];
                    tmp_inData[1]   = packed_planes[1];
                    tmp_inData[2]   = packed_planes[2];
                    tmp_inStride[0] = packed_lines[0];
                    tmp_inStride[1] = packed_lines[1];
                    tmp_inStride[2] = packed_lines[2];
                    used_temp       = true;
                    tmp_y           = packed_buf;
                }
                else
                {
                    av_free(packed_buf);
                }
            }
        }
        if (!used_temp)
        {
            int w = frame->width;
            int h = frame->height;
            if (srcFmt == AV_PIX_FMT_YUV420P)
            {
                int wh = w * h;
                int hw = (w / 2) * (h / 2);
                tmp_y  = (uint8_t*)av_malloc((size_t)wh);
                tmp_u  = (uint8_t*)av_malloc((size_t)hw);
                tmp_v  = (uint8_t*)av_malloc((size_t)hw);
                if (tmp_y && tmp_u && tmp_v)
                {
                    used_temp = true;
                    for (int y = 0; y < h; ++y)
                    {
                        const uint8_t* src = frame->data[0] + (size_t)y * frame->linesize[0];
                        uint8_t* dstrow    = tmp_y + (size_t)y * w;
                        memcpy(dstrow, src, (size_t)w);
                    }
                    for (int y = 0; y < h / 2; ++y)
                    {
                        const uint8_t* src_u = frame->data[1] + (size_t)y * frame->linesize[1];
                        const uint8_t* src_v = frame->data[2] + (size_t)y * frame->linesize[2];
                        uint8_t* dstu        = tmp_u + (size_t)y * (w / 2);
                        uint8_t* dstv        = tmp_v + (size_t)y * (w / 2);
                        memcpy(dstu, src_u, (size_t)(w / 2));
                        memcpy(dstv, src_v, (size_t)(w / 2));
                    }

                    tmp_inData[0]   = tmp_y;
                    tmp_inData[1]   = tmp_u;
                    tmp_inData[2]   = tmp_v;
                    tmp_inStride[0] = w;
                    tmp_inStride[1] = w / 2;
                    tmp_inStride[2] = w / 2;
                }
            }
            else if (srcFmt == AV_PIX_FMT_NV12)
            {
                int wh          = w * h;
                int uv_size     = w * (h / 2);
                tmp_y           = (uint8_t*)av_malloc((size_t)wh);
                uint8_t* tmp_uv = (uint8_t*)av_malloc((size_t)uv_size);
                if (tmp_y && tmp_uv)
                {
                    used_temp = true;
                    for (int y = 0; y < h; ++y)
                    {
                        const uint8_t* src = frame->data[0] + (size_t)y * frame->linesize[0];
                        uint8_t* dstrow    = tmp_y + (size_t)y * w;
                        memcpy(dstrow, src, (size_t)w);
                    }
                    for (int y = 0; y < h / 2; ++y)
                    {
                        const uint8_t* src_uv = frame->data[1] + (size_t)y * frame->linesize[1];
                        uint8_t* dstrow       = tmp_uv + (size_t)y * w;
                        memcpy(dstrow, src_uv, (size_t)w);
                    }

                    tmp_inData[0]   = tmp_y;
                    tmp_inData[1]   = tmp_uv;
                    tmp_inStride[0] = w;
                    tmp_inStride[1] = w;
                    tmp_inStride[2] = 0;
                    tmp_u           = tmp_uv;
                }
            }
            else
            {
                VITA_DEBUG_LOG("[FFMPEG] publish_sw_frame: unsupported srcFmt %d for VRAM->RAM copy", (int)srcFmt);
            }
        }
    }

    if (verboseSwLog)
    {
        VITA_DEBUG_LOG("[FFMPEG] sws_scale: inStride[0]=%d inStride[1]=%d inStride[2]=%d outStride[0]=%d sw_texture_stride=%d",
            tmp_inStride[0], tmp_inStride[1], tmp_inStride[2], outStride[0], dstStride);
    }
    uint32_t swsStartUs   = perf_now_us();
    int swsRet            = sws_scale(ctx->sws_context, (const uint8_t**)tmp_inData, tmp_inStride, 0, frame->height, outData, outStride);
    uint32_t swsElapsedUs = perf_now_us() - swsStartUs;
    g_perf.sws_calls++;
    g_perf.sws_total_us += swsElapsedUs;
    if (swsElapsedUs > g_perf.sws_max_us)
    {
        g_perf.sws_max_us = swsElapsedUs;
    }
    if (verboseSwLog)
    {
        VITA_DEBUG_LOG("[FFMPEG] sws_scale returned %d", swsRet);
    }

    // Diagnostics: treat only negative values as errors.
    // On some Vita FFmpeg builds sws_scale may return 0 while still producing output.
    if (swsRet < 0)
    {
        bool p0_vram = is_vram_ptr(inData[0]);
        bool p1_vram = is_vram_ptr(inData[1]);
        bool p2_vram = is_vram_ptr(inData[2]);
        VITA_DEBUG_LOG("[FFMPEG] sws_scale failed diagnostics: srcFmt=%d used_temp=%d p0_vram=%d p1_vram=%d p2_vram=%d",
            (int)srcFmt, used_temp ? 1 : 0, p0_vram ? 1 : 0, p1_vram ? 1 : 0, p2_vram ? 1 : 0);

        if (used_temp && tmp_y)
        {
            VITA_DEBUG_LOG("[FFMPEG] tmp_y first bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                tmp_y[0], tmp_y[1], tmp_y[2], tmp_y[3], tmp_y[4], tmp_y[5], tmp_y[6], tmp_y[7]);
        }
        else if (inData[0] && !p0_vram)
        {
            const uint8_t* p = inData[0];
            VITA_DEBUG_LOG("[FFMPEG] plane0 first bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        }

        if (used_temp && tmp_u)
        {
            VITA_DEBUG_LOG("[FFMPEG] tmp_u first bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                tmp_u[0], tmp_u[1], tmp_u[2], tmp_u[3], tmp_u[4], tmp_u[5], tmp_u[6], tmp_u[7]);
        }
        else if (inData[1] && !p1_vram)
        {
            const uint8_t* p = inData[1];
            VITA_DEBUG_LOG("[FFMPEG] plane1 first bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        }

        if (used_temp && tmp_v)
        {
            VITA_DEBUG_LOG("[FFMPEG] tmp_v first bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                tmp_v[0], tmp_v[1], tmp_v[2], tmp_v[3], tmp_v[4], tmp_v[5], tmp_v[6], tmp_v[7]);
        }
        else if (inData[2] && !p2_vram)
        {
            const uint8_t* p = inData[2];
            VITA_DEBUG_LOG("[FFMPEG] plane2 first bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        }

        brls::Logger::error("[FFMPEG] sws_scale failed");
        if (used_temp)
        {
            av_free(tmp_y);
            av_free(tmp_u);
            av_free(tmp_v);
        }
        if (rgba_tmp)
        {
            av_free(rgba_tmp);
            rgba_tmp = nullptr;
        }
        return false;
    }

    if (swsRet == 0 && verboseSwLog)
    {
        VITA_DEBUG_LOG("[FFMPEG] sws_scale returned 0; accepting frame as valid on Vita path");
    }

    if (used_temp)
    {
        av_free(tmp_y);
        av_free(tmp_u);
        av_free(tmp_v);
    }

    if (used_out_tmp && rgba_tmp)
    {
        for (int yy = 0; yy < h; ++yy)
        {
            uint8_t* dstrow = dst + (size_t)yy * (size_t)dstStride;
            uint8_t* srcrow = rgba_tmp + (size_t)yy * (size_t)rgba_stride;
            memcpy(dstrow, srcrow, (size_t)rgba_stride);
        }
        av_free(rgba_tmp);
        rgba_tmp = nullptr;
    }

    rotate_sw_ring_and_publish(ctx, writeTex, frame->width, frame->height, dstStride);
    g_perf.published_frames++;
    return true;
#endif
}

static bool publish_frame(FFmpegVideoContext* ctx, AVFrame* frame, uint64_t ptsUs)
{
    if (!ctx || !frame)
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_frame: null ctx or frame");
        return false;
    }

    if (g_ffmpeg_stop_request.load(std::memory_order_acquire))
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_frame: dropping frame due to stop request");
        reset_global_slots();
        ctx->current_frame.texture       = nullptr;
        ctx->current_frame.width         = 0;
        ctx->current_frame.height        = 0;
        ctx->current_frame.has_frame     = false;
        ctx->current_frame.direct_memory = false;
        ctx->using_direct_memory         = false;
        VideoFrameHolder::instance().clear();
        return false;
    }

    static uint32_t s_publish_log_counter = 0;
    if ((s_publish_log_counter++ % 1200) == 0)
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_frame: pts=%llu format=%d %dx%d", ptsUs, frame->format, frame->width, frame->height);
    }

    bool published = false;
#ifdef BOREALIS_USE_GXM
    if (ctx->decoder.use_direct_render)
    {
        published = publish_direct_frame(ctx, frame);
    }
#endif
    if (!published)
    {
        published = publish_sw_frame(ctx, frame);
    }

    if (!published || !ctx->current_frame.texture)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> slotLock(g_frame_slots_mutex);
        frame_textures[0]   = nullptr;
        frame_textures[1]   = nullptr;
        frame_textures[2]   = nullptr;
        frame_display_idx   = 0;
        frame_ready_idx     = 0;
        frame_write_idx     = 0;
        single_frame_buffer = true;

        if (ctx->using_direct_memory)
        {
#ifdef BOREALIS_USE_GXM
            // Use dedicated dr_textures for double-buffering so NVG re-creates image ids
            dr_texture* front   = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_front_idx]);
            dr_texture* back    = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_back_idx]);
            frame_textures[0]   = front ? &front->impl : ctx->current_frame.texture;
            frame_textures[1]   = back ? &back->impl : ctx->current_frame.texture;
            frame_display_idx   = 0;
            frame_ready_idx     = 1;
            frame_write_idx     = 1;
            single_frame_buffer = false;
#else
            frame_textures[0]   = ctx->current_frame.texture;
            frame_textures[1]   = ctx->current_frame.texture;
            frame_display_idx   = 0;
            frame_ready_idx     = 0;
            frame_write_idx     = 0;
            single_frame_buffer = true;
#endif
        }
        else
        {
            frame_textures[0]   = ctx->current_frame.texture;
            frame_textures[1]   = ctx->current_frame.texture;
            frame_display_idx   = 0;
            frame_ready_idx     = 0;
            frame_write_idx     = 0;
            single_frame_buffer = true;
        }
    }

    uint32_t texW  = ctx->current_frame.width > 0 ? (uint32_t)ctx->current_frame.width : image_scaling.texture_width;
    uint32_t texH  = ctx->current_frame.height > 0 ? (uint32_t)ctx->current_frame.height : image_scaling.texture_height;
    uint64_t ptsMs = ptsUs ? (ptsUs / 1000ULL) : monotonic_ms_local();

#ifdef BOREALIS_USE_GXM
    if (ctx->using_direct_memory)
    {
        // Direct path: avoid per-frame blocking GPU waits from decode thread.
        // Texture recycling is handled by triple buffering in publish_direct_frame.
    }
#endif

    ctx->last_pts_us = ptsUs;
    VideoFrameHolder::instance().pushTexture(ctx->current_frame.texture, texW, texH, ptsMs);
    VitaSession::onFrameDecoded();
    return true;
}

static void ffmpeg_release_locked(FFmpegVideoContext* ctx)
{
    if (!ctx)
    {
        return;
    }

#ifdef BOREALIS_USE_GXM
    for (int i = 0; i < 3; ++i)
    {
        if (ctx->dr_textures[i])
        {
            dr_texture* tex = reinterpret_cast<dr_texture*>(ctx->dr_textures[i]);
            dr_texture_free(&tex);
            ctx->dr_textures[i] = nullptr;
        }
    }
#endif
    for (int i = 0; i < 3; ++i)
    {
        if (ctx->sw_textures[i])
        {
            gxm_texture_free(ctx->sw_textures[i]);
            ctx->sw_textures[i] = nullptr;
        }
    }
    ctx->sw_texture = nullptr;
    if (ctx->sws_context)
    {
        sws_freeContext(ctx->sws_context);
        ctx->sws_context = nullptr;
    }
    if (ctx->decoder.initialized)
    {
        ffmpeg_decoder_destroy(&ctx->decoder);
    }
#ifdef BOREALIS_USE_GXM
    ffmpeg_flush_deferred_releases();
#endif
    memset(&ctx->decoder, 0, sizeof(ctx->decoder));
    memset(&ctx->current_frame, 0, sizeof(ctx->current_frame));
    ctx->sw_texture_width    = 0;
    ctx->sw_texture_height   = 0;
    ctx->sw_texture_stride   = 0;
    ctx->sw_texture_format   = 0;
    ctx->sws_src_w           = 0;
    ctx->sws_src_h           = 0;
    ctx->sws_src_fmt         = -1;
    ctx->sw_write_idx        = 0;
    ctx->sw_last_present_idx = 0;
    ctx->using_direct_memory = false;
    ctx->initialized         = false;
    ctx->last_pts_us         = 0;
    reset_global_slots();
}

extern "C"
{

    int ffmpeg_video_init(FFmpegVideoContext* context, int width, int height, int frame_rate)
    {
        if (!context)
        {
            return -1;
        }

        // Ensure renderer isn't holding the texture handle before we free anything
        // Clear any pending frame so the renderer won't use it and wait for GPU to finish.
        VideoFrameHolder::instance().clear();
        wait_for_borealis_gxm_idle();

        memset(context, 0, sizeof(*context));
        context->dr_textures[0]      = nullptr;
        context->dr_textures[1]      = nullptr;
        context->dr_textures[2]      = nullptr;
        context->dr_front_idx        = 0;
        context->dr_back_idx         = 1;
        context->dr_spare_idx        = 2;
        context->frame_rate          = frame_rate;
        context->stream_width        = width;
        context->stream_height       = height;
        context->render_mode         = "ffmpeg";
        context->is_legacy_mode      = false;
        context->using_direct_memory = false;
        reset_global_slots();
        return 0;
    }

    void ffmpeg_video_cleanup(FFmpegVideoContext* context)
    {
        g_ffmpeg_stop_request.store(true, std::memory_order_release);
        int waitCount = 0;
        while (g_active_decodes.load(std::memory_order_acquire) > 0 && waitCount < 5000)
        {
            sceKernelDelayThread(1000);
            waitCount++;
        }
        if (waitCount >= 5000)
        {
            VITA_DEBUG_LOG("[FFMPEG] ffmpeg_video_cleanup: wait for decodes timed out (active=%d)", g_active_decodes.load());
        }
        std::lock_guard<std::mutex> lock(g_ffmpeg_mutex);
        if (g_ffmpeg_context == context)
        {
            g_ffmpeg_context = nullptr;
        }
        ffmpeg_release_locked(context);
        g_ffmpeg_stop_request.store(false, std::memory_order_release);
    }

    int ffmpeg_video_decode(FFmpegVideoContext* context, unsigned char* data, int size, int frame_type)
    {
        (void)frame_type;
        if (!context || !context->decoder.initialized)
        {
            return AVERROR(EINVAL);
        }
        return ffmpeg_decoder_decode(&context->decoder, data, size);
    }

    void ffmpeg_video_start(FFmpegVideoContext* context)
    {
        if (!context)
        {
            return;
        }
        stats_start_ms     = monotonic_ms_local();
        g_stats.target_fps = context->frame_rate > 0 ? (uint32_t)context->frame_rate : 60;
    }

    static void ffmpeg_video_stop_locked(FFmpegVideoContext* context)
    {
        if (!context)
            return;
        if (context->decoder.initialized)
        {
            ffmpeg_decoder_destroy(&context->decoder);
            context->decoder.initialized = false;
        }
        context->initialized = false;

        // Ensure the NVG image is dropped before we invalidate/free the GXM textures
        // associated with the stream to avoid Borealis sampling freed VRAM.
#ifdef BOREALIS_USE_GXM
        VitaVideoRenderer::instance().destroyImage(nullptr);
#endif

        // Ensure the renderer won't use textures about to be freed.
        VideoFrameHolder::instance().clear();
        reset_global_slots();
        context->current_frame.texture       = nullptr;
        context->current_frame.width         = 0;
        context->current_frame.height        = 0;
        context->current_frame.has_frame     = false;
        context->current_frame.direct_memory = false;
        context->using_direct_memory         = false;
        wait_for_borealis_gxm_idle();
        process_pending_vram_frees_if_safe();
        ffmpeg_flush_deferred_releases();
    }

    void ffmpeg_video_stop(FFmpegVideoContext* context)
    {
        // Signal stop request: prevent new decodes from starting, and wait for in-flight
        // decodes to finish before destroying resources.
        g_ffmpeg_stop_request.store(true, std::memory_order_release);
        // Immediately clear any renderer-held references so the UI stops sampling
        // the soon-to-be-destroyed textures while we wait for the decoder to drain.
#ifdef BOREALIS_USE_GXM
        VitaVideoRenderer::instance().destroyImage(nullptr);
#endif
        VideoFrameHolder::instance().clear();
        reset_global_slots();
        // Wait with a timeout in a loop to avoid deadlock. Decodes increment and
        // decrement g_active_decodes; wait until it reaches zero or timeout.
        int waitCount = 0;
        while (g_active_decodes.load(std::memory_order_acquire) > 0 && waitCount < 5000)
        {
            sceKernelDelayThread(1000);
            waitCount++;
        }
        if (waitCount >= 5000)
        {
            VITA_DEBUG_LOG("[FFMPEG] ffmpeg_video_stop: wait for decodes timed out (active=%d)", g_active_decodes.load());
        }
        std::lock_guard<std::mutex> lock(g_ffmpeg_mutex);
        ffmpeg_video_stop_locked(context);
        g_ffmpeg_stop_request.store(false, std::memory_order_release);
    }

    void ffmpeg_video_render(FFmpegVideoContext* context)
    {
        (void)context;
    }

    static int ffmpeg_video_setup(int videoFormat, int width, int height, int redrawRate, void* ctxPtr, int drFlags)
    {
        (void)videoFormat;
        (void)drFlags;

        auto* context = static_cast<FFmpegVideoContext*>(ctxPtr);
        if (!context)
        {
            brls::Logger::error("[FFMPEG] setup received null context");
            VITA_DEBUG_LOG("[FFMPEG] setup received null context");
            return -1;
        }

        context->current_frame.texture       = nullptr;
        context->current_frame.width         = 0;
        context->current_frame.height        = 0;
        context->current_frame.has_frame     = false;
        context->current_frame.direct_memory = false;
        context->using_direct_memory         = false;

        VITA_DEBUG_LOG("[FFMPEG] setup %dx%d @ %dfps ctx=%p", width, height, redrawRate, context);
        std::lock_guard<std::mutex> lock(g_ffmpeg_mutex);
        brls::Logger::info("[FFMPEG] setup {}x{} @ {}fps ctx={:#x}", width, height, redrawRate, (uintptr_t)context);

        {
            std::lock_guard<std::mutex> slotLock(g_frame_slots_mutex);
            if (frame_textures[0])
            {
                gxm_texture_free(frame_textures[0]);
                frame_textures[0] = nullptr;
            }
            if (frame_textures[1])
            {
                gxm_texture_free(frame_textures[1]);
                frame_textures[1] = nullptr;
            }
            if (frame_textures[2])
            {
                gxm_texture_free(frame_textures[2]);
                frame_textures[2] = nullptr;
            }
        }

        ffmpeg_release_locked(context);
        if (ffmpeg_video_init(context, width, height, redrawRate) < 0)
        {
            brls::Logger::error("[FFMPEG] init failed");
            return -1;
        }

#ifdef BOREALIS_USE_GXM
        wait_for_borealis_gxm_idle();
#endif

        vitavideo_configure_screen_resolution(width);
        vitavideo_update_scaling_settings(width, height);
        video_fullscreen_stretch = g_video_settings_snapshot.fullscreen;

        memset(&g_stats, 0, sizeof(g_stats));
        stats_start_ms     = 0;
        last_fps_window_ms = 0;
        frame_count        = 0;
        need_drop          = 0;
        g_stats.target_fps = redrawRate > 0 ? (uint32_t)redrawRate : 60;
        vita_netopt_set_target_fps(g_stats.target_fps);
        g_ffmpeg_submit_counter = 0;
        g_ffmpeg_frame_index    = 0;

        if (ffmpeg_decoder_init(&context->decoder) < 0)
        {
            brls::Logger::error("[FFMPEG] decoder init failed");
            ffmpeg_release_locked(context);
            return -1;
        }
        if (context->decoder.use_direct_render)
        {
            VITA_DEBUG_LOG("[FFMPEG] Decoder initialized with direct render enabled");
        }
        else
        {
            VITA_DEBUG_LOG("[FFMPEG] Decoder initialized with software render path (no direct render)");
        }

        context->initialized = true;
        g_ffmpeg_context     = context;
        reset_global_slots();
        return 0;
    }

    static void ffmpeg_video_start_callback(void)
    {
        std::lock_guard<std::mutex> lock(g_ffmpeg_mutex);
        if (g_ffmpeg_context)
        {
            ffmpeg_video_start(g_ffmpeg_context);
        }
    }

    static void ffmpeg_video_stop_callback(void)
    {
        FFmpegVideoContext* context = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_ffmpeg_mutex);
            context = g_ffmpeg_context;
        }
        if (context)
        {
            ffmpeg_video_stop(context);
        }
    }

    static void ffmpeg_video_cleanup_callback(void)
    {
        g_ffmpeg_stop_request.store(true, std::memory_order_release);
        int waitCount = 0;
        while (g_active_decodes.load(std::memory_order_acquire) > 0 && waitCount < 5000)
        {
            sceKernelDelayThread(1000);
            waitCount++;
        }
        if (waitCount >= 5000)
        {
            VITA_DEBUG_LOG("[FFMPEG] ffmpeg_video_cleanup_callback: wait for decodes timed out (active=%d)", g_active_decodes.load());
        }
        std::lock_guard<std::mutex> lock(g_ffmpeg_mutex);
        if (g_ffmpeg_context)
        {
            ffmpeg_release_locked(g_ffmpeg_context);
            g_ffmpeg_context = nullptr;
        }
        g_ffmpeg_stop_request.store(false, std::memory_order_release);
    }

    static int ffmpeg_video_submit_decode_unit(PDECODE_UNIT decodeUnit)
    {
        g_perf.submit_calls++;
        uint32_t submitStartUs = perf_now_us();
        uint32_t lockWaitUs    = 0;
        uint32_t drainUs       = 0;
        uint32_t copyUs        = 0;
        uint32_t sendUs        = 0;
        uint32_t recvUs        = 0;

        auto update_max_u32 = [](uint32_t& dst, uint32_t value)
        {
            if (value > dst)
                dst = value;
        };

        auto finalize_submit_metrics = [&]()
        {
            uint32_t submitUs = perf_now_us() - submitStartUs;
            g_perf.submit_total_us += submitUs;
            update_max_u32(g_perf.submit_max_us, submitUs);
            g_perf.lock_wait_total_us += lockWaitUs;
            update_max_u32(g_perf.lock_wait_max_us, lockWaitUs);
            g_perf.drain_total_us += drainUs;
            update_max_u32(g_perf.drain_max_us, drainUs);
            g_perf.copy_total_us += copyUs;
            update_max_u32(g_perf.copy_max_us, copyUs);
            g_perf.send_total_us += sendUs;
            update_max_u32(g_perf.send_max_us, sendUs);
            g_perf.recv_total_us += recvUs;
            update_max_u32(g_perf.recv_max_us, recvUs);
        };

        // Low-latency drop control.
        // By default, automatic dropping is OFF (it can oscillate and increase jitter).
        // Manual override: MOONLIGHT_FFMPEG_FORCE_DROP_EVERY=N.
        // Optional auto mode: MOONLIGHT_FFMPEG_AUTO_DROP=1.
        static uint32_t s_drop_phase      = 0;
        static int s_forcedDropEvery      = -1;
        static int s_autoDropEnabled      = -1;
        static int s_dropStaleDrainFrames = -1;
        if (s_forcedDropEvery < 0)
        {
            const char* forcedEnv = getenv("MOONLIGHT_FFMPEG_FORCE_DROP_EVERY");
            if (forcedEnv)
            {
                int v = atoi(forcedEnv);
                if (v < 0)
                    v = 0;
                s_forcedDropEvery = v;
                VITA_DEBUG_LOG("[FFMPEG][LAT] forced drop_every=%d (env)", s_forcedDropEvery);
            }
            else
            {
                s_forcedDropEvery = 0;
            }
        }
        if (s_autoDropEnabled < 0)
        {
            const char* autoEnv = getenv("MOONLIGHT_FFMPEG_AUTO_DROP");
            s_autoDropEnabled   = (autoEnv && autoEnv[0] == '1') ? 1 : 0;
            VITA_DEBUG_LOG("[FFMPEG][LAT] auto_drop=%d (env)", s_autoDropEnabled);
        }
        if (s_dropStaleDrainFrames < 0)
        {
            const char* staleEnv = getenv("MOONLIGHT_FFMPEG_DROP_STALE_DRAIN");
            // Default OFF: stale frames drained before send are asynchronous completions on Vita, do not drop.
            s_dropStaleDrainFrames = (staleEnv && staleEnv[0] == '1') ? 1 : 0;
            VITA_DEBUG_LOG("[FFMPEG][LAT] drop_stale_drain=%d (env)", s_dropStaleDrainFrames);
        }

        uint32_t dropEvery = 0;
        uint32_t targetFps = g_stats.target_fps ? g_stats.target_fps : 60;
        if (s_forcedDropEvery > 0)
        {
            dropEvery = (uint32_t)s_forcedDropEvery;
        }
        else if (s_autoDropEnabled && targetFps > 0 && g_perf.submit_calls >= 30)
        {
            uint32_t frameBudgetUs = 1000000U / targetFps;
            uint32_t submitAvgUs   = (uint32_t)(g_perf.submit_total_us / g_perf.submit_calls);
            if (submitAvgUs > frameBudgetUs)
            {
                uint32_t overUs  = submitAvgUs - frameBudgetUs;
                uint32_t overPct = (uint32_t)(((uint64_t)overUs * 100ULL) / (frameBudgetUs ? frameBudgetUs : 1));

                // More aggressive when we are clearly above budget to keep latency low.
                if (overPct >= 10)
                    dropEvery = 4;
                else if (overPct >= 7)
                    dropEvery = 5;
                else if (overPct >= 5)
                    dropEvery = 6;
                else if (overPct >= 3)
                    dropEvery = 8;
                else
                    dropEvery = 10;
            }
        }

        if (dropEvery > 12)
            dropEvery = 12;
        g_perf.adaptive_drop_interval = dropEvery;

        if (!decodeUnit)
        {
            VITA_DEBUG_LOG("[FFMPEG] submit_decode_unit: null decodeUnit");
            finalize_submit_metrics();
            return DR_NEED_IDR;
        }

        if (g_ffmpeg_stop_request.load(std::memory_order_acquire))
        {
            finalize_submit_metrics();
            return DR_NEED_IDR;
        }

        if (dropEvery > 0 && decodeUnit->frameType != FRAME_TYPE_IDR)
        {
            s_drop_phase++;
            if ((s_drop_phase % dropEvery) == 0)
            {
                g_perf.dropped_decode_units++;
                g_stats.frames_dropped_pacer++;
                finalize_submit_metrics();
                perf_report_if_due();
                return DR_OK;
            }
        }

        bool verboseDecodeLog = ((g_ffmpeg_submit_counter % 600) == 0);
        if (verboseDecodeLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] submit_decode_unit: size=%d pts=%llu", decodeUnit->fullLength, decodeUnit->presentationTimeUs);
        }

        uint32_t lockStartUs = perf_now_us();
        std::unique_lock<std::mutex> lock(g_ffmpeg_mutex);
        lockWaitUs = perf_now_us() - lockStartUs;
        // Track active decodes so vram_free can defer frees while we are decoding.
        struct ActiveDecodeGuard
        {
            ActiveDecodeGuard() { g_active_decodes.fetch_add(1, std::memory_order_acq_rel); }
            ~ActiveDecodeGuard()
            {
                int prev = g_active_decodes.fetch_sub(1, std::memory_order_acq_rel);
                int now  = prev - 1;
                if (now == 0)
                {
                    process_pending_vram_frees_if_safe();
                }
            }
        } _guard;
        FFmpegVideoContext* context = g_ffmpeg_context;
        if (!context || !context->initialized || !context->decoder.initialized || g_ffmpeg_stop_request.load(std::memory_order_acquire))
        {
            // The decode unit memory is owned by the depacketizer/submitter. Do not free here
            // (freeing it here caused double-free/data-abort). The caller of submitDecodeUnit
            // (reassembleFrame) will handle completion/freeing via LiCompleteVideoFrame.
            finalize_submit_metrics();
            return DR_NEED_IDR;
        }

        AVCodecContext* avctx = context->decoder.avctx;
        AVPacket* pkt         = context->decoder.pkt;
        AVFrame* frame        = context->decoder.frame;

        uint32_t drainStartUs   = perf_now_us();
        bool drainReceived      = false;
        AVFrame* tempDrainFrame = av_frame_alloc();
        if (tempDrainFrame)
        {
            while (true)
            {
                int drain = avcodec_receive_frame(avctx, tempDrainFrame);
                if (verboseDecodeLog)
                {
                    VITA_DEBUG_LOG("[FFMPEG] avcodec_receive_frame drain ret=%d", drain);
                }
                if (drain == AVERROR(EAGAIN) || drain == AVERROR_EOF)
                {
                    break;
                }
                if (drain < 0)
                {
                    brls::Logger::error("[FFMPEG] receive_frame drain error: 0x{:X}", drain);
                    break;
                }
                if (drainReceived)
                {
                    av_frame_unref(frame);
                    g_perf.dropped_stale_frames++;
                }
                av_frame_move_ref(frame, tempDrainFrame);
                drainReceived = true;
            }
            av_frame_free(&tempDrainFrame);
        }
        if (drainReceived)
        {
            if (s_dropStaleDrainFrames)
            {
                g_perf.dropped_stale_frames++;
                av_frame_unref(frame);
            }
            else
            {
                if (publish_frame(context, frame, context->last_pts_us))
                {
                    av_frame_unref(frame);
                }
            }
        }
        drainUs = perf_now_us() - drainStartUs;

        if (verboseDecodeLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] after drain loop");
            VITA_DEBUG_LOG("[FFMPEG] before av_new_packet size=%d", decodeUnit->fullLength);
            VITA_DEBUG_LOG("[FFMPEG] pkt=%p avctx=%p frame=%p", pkt, avctx, frame);
        }
        uint32_t copyStartUs = perf_now_us();
        if (av_new_packet(pkt, decodeUnit->fullLength) < 0)
        {
            brls::Logger::error("[FFMPEG] av_new_packet failed size={}", decodeUnit->fullLength);
            // Do not free decodeUnit here; ownership belongs to the depacketizer.
            copyUs = perf_now_us() - copyStartUs;
            finalize_submit_metrics();
            return DR_NEED_IDR;
        }
        if (verboseDecodeLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] av_new_packet ok, pkt->data=%p", pkt->data);
        }

        uint8_t* dst  = pkt->data;
        PLENTRY entry = decodeUnit->bufferList;
        while (entry)
        {
            memcpy(dst, entry->data, entry->length);
            dst += entry->length;
            entry = entry->next;
        }
        copyUs = perf_now_us() - copyStartUs;

        pkt->pts = decodeUnit->presentationTimeUs;
        pkt->dts = decodeUnit->presentationTimeUs;
        if (decodeUnit->frameType == FRAME_TYPE_IDR)
        {
            pkt->flags |= AV_PKT_FLAG_KEY;
        }
        else
        {
            pkt->flags &= ~AV_PKT_FLAG_KEY;
        }

        uint32_t sendStartUs = perf_now_us();
        int sendRes          = avcodec_send_packet(avctx, pkt);
        sendUs               = perf_now_us() - sendStartUs;
        av_packet_unref(pkt);
        if (verboseDecodeLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] after send_packet res=%d", sendRes);
        }
        if (sendRes < 0 && sendRes != AVERROR(EAGAIN))
        {
            brls::Logger::error("[FFMPEG] send_packet error=0x{:X}", sendRes);
            finalize_submit_metrics();
            return DR_NEED_IDR;
        }

        vita_netopt_on_frame_seen(g_ffmpeg_frame_index);

        uint32_t recvStartUs = perf_now_us();
        bool frameReceived   = false;
        AVFrame* tempFrame   = av_frame_alloc();
        if (tempFrame)
        {
            while (true)
            {
                int ret = avcodec_receive_frame(avctx, tempFrame);
                if (verboseDecodeLog)
                {
                    VITA_DEBUG_LOG("[FFMPEG] avcodec_receive_frame ret=%d", ret);
                }
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                if (ret < 0)
                {
                    brls::Logger::error("[FFMPEG] receive_frame error=0x{:X}", ret);
                    av_frame_free(&tempFrame);
                    // Do not free decodeUnit here; caller (depacketizer) handles freeing.
                    recvUs = perf_now_us() - recvStartUs;
                    finalize_submit_metrics();
                    return DR_NEED_IDR;
                }
                if (frameReceived)
                {
                    av_frame_unref(frame);
                    g_perf.dropped_stale_frames++;
                }
                av_frame_move_ref(frame, tempFrame);
                frameReceived = true;
            }
            av_frame_free(&tempFrame);
        }
        recvUs = perf_now_us() - recvStartUs;

        if (frameReceived)
        {
            uint32_t dec_ms = (sendUs + recvUs) / 1000;
            g_stats.decode_time_ms += dec_ms;
            if (dec_ms < g_decode_min_ms)
                g_decode_min_ms = dec_ms;
            if (dec_ms > g_decode_max_ms)
                g_decode_max_ms = dec_ms;
            g_decode_sum_ms += dec_ms;
            g_decode_count++;
            if (publish_frame(context, frame, decodeUnit->presentationTimeUs))
            {
                if (stats_start_ms == 0)
                {
                    stats_start_ms = monotonic_ms_local();
                }
                g_stats.frames_decoded++;
                g_perf.decoded_frames++;
                frame_count++;
                vita_netopt_frame_produced();
                vita_netopt_on_frame_completed(g_ffmpeg_frame_index);
                g_ffmpeg_frame_index++;
            }
            av_frame_unref(frame);
        }

        g_ffmpeg_submit_counter++;
        process_pending_vram_frees_if_safe();
        finalize_submit_metrics();
        perf_report_if_due();
        // Do not free decodeUnit here; the depacketizer will call LiCompleteVideoFrame after
        // evaluating our return value and will perform any necessary frees.
        return DR_OK;
    }

    DECODER_RENDERER_CALLBACKS get_ffmpeg_video_callbacks(void)
    {
        DECODER_RENDERER_CALLBACKS callbacks = { 0 };
        callbacks.setup                      = ffmpeg_video_setup;
        callbacks.start                      = ffmpeg_video_start_callback;
        callbacks.stop                       = ffmpeg_video_stop_callback;
        callbacks.cleanup                    = ffmpeg_video_cleanup_callback;
        callbacks.submitDecodeUnit           = ffmpeg_video_submit_decode_unit;
        // Removing CAPABILITY_DIRECT_SUBMIT decouples network packet reception from hardware decoding.
        // This allows the high-priority receive thread to continuously ingest UDP packets without
        // blocking for 14ms on avcodec_send_packet, avoiding socket buffer overflow and packet loss,
        // which results in a rock-stable 60/60 FPS stream.
        callbacks.capabilities = CAPABILITY_SLICES_PER_FRAME(2);
        return callbacks;
    }

    void ffmpeg_video_set_render_mode(FFmpegVideoContext* context, const char* mode)
    {
        if (!context)
        {
            return;
        }
        context->render_mode    = mode;
        context->is_legacy_mode = (mode && strcmp(mode, "legacy") == 0);
    }

    const char* ffmpeg_video_get_render_mode(FFmpegVideoContext* context)
    {
        return context ? context->render_mode : "ffmpeg";
    }

} // extern "C"
