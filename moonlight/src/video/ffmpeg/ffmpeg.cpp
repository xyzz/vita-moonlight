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
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <vita2d.h>
#endif

static FFmpegVideoContext* g_ffmpeg_context = nullptr;
static std::mutex g_ffmpeg_mutex;
static std::atomic<int> g_active_decodes { 0 };
static std::atomic<bool> g_ffmpeg_stop_request { false };
static uint32_t g_ffmpeg_submit_counter = 0;
static unsigned g_ffmpeg_frame_index    = 0;

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
// Track VRAM mapping counts and deferred frees by memblock id.
static std::mutex g_vram_mutex;
static std::unordered_map<SceUID, int> g_vram_map_count;
static std::unordered_map<SceUID, uint64_t> g_vram_pending_free;
static std::atomic<uint64_t> g_decode_epoch { 0 };
static const uint64_t k_vram_free_grace_epochs = 16;

static void process_pending_vram_frees_if_safe()
{
    std::lock_guard<std::mutex> lock(g_vram_mutex);
    bool decoderActive = (g_active_decodes.load(std::memory_order_acquire) > 0);
    bool stopRequested = g_ffmpeg_stop_request.load(std::memory_order_acquire);
    if (decoderActive || stopRequested)
    {
        return;
    }
    if (!g_vram_pending_free.empty())
    {
        std::vector<SceUID> to_free;
        to_free.reserve(g_vram_pending_free.size());
        uint64_t nowEpoch = g_decode_epoch.load(std::memory_order_acquire);
        for (const auto& pending : g_vram_pending_free)
        {
            SceUID mb           = pending.first;
            uint64_t deferEpoch = pending.second;
            auto it             = g_vram_map_count.find(mb);
            bool unmapped       = (it == g_vram_map_count.end() || it->second == 0);
            bool graceElapsed   = (nowEpoch >= deferEpoch) && ((nowEpoch - deferEpoch) >= k_vram_free_grace_epochs);
            if (unmapped && graceElapsed)
            {
                to_free.push_back(mb);
            }
        }
        for (SceUID mb : to_free)
        {
            auto pit            = g_vram_pending_free.find(mb);
            uint64_t deferEpoch = (pit != g_vram_pending_free.end()) ? pit->second : 0;
            g_vram_pending_free.erase(mb);
            sceKernelFreeMemBlock(mb);
            VITA_DEBUG_LOG("[FFMPEG] process_pending_vram_frees_if_safe: freed memblock %d (epoch_delta=%llu)",
                mb,
                (unsigned long long)(nowEpoch - deferEpoch));
        }
    }
}

static void vram_free(void* opaque, uint8_t* data)
{
    // Do NOT unmap memory here; dr_texture_detach is responsible for unmapping
    // to avoid double-unmap race conditions. Only free the memblock.
    SceUID mb = (SceUID)(intptr_t)opaque;
    (void)data; // data may be NULL
    if (mb == 0)
        return;
    std::lock_guard<std::mutex> lock(g_vram_mutex);
    auto it            = g_vram_map_count.find(mb);
    bool isMapped      = (it != g_vram_map_count.end() && it->second > 0);
    bool decoderActive = (g_active_decodes.load(std::memory_order_acquire) > 0);
    if (decoderActive || isMapped || g_ffmpeg_stop_request.load(std::memory_order_acquire))
    {
        // Some dr_texture still has mapped VRAM: defer free until unmapped.
        VITA_DEBUG_LOG("[FFMPEG] vram_free: memblock %d map_count=%d decoderActive=%d stop_request=%d -> defer free", mb, isMapped ? it->second : 0, decoderActive ? 1 : 0, (int)g_ffmpeg_stop_request.load());
        uint64_t nowEpoch = g_decode_epoch.load(std::memory_order_acquire);
        auto existing     = g_vram_pending_free.find(mb);
        if (existing == g_vram_pending_free.end())
        {
            g_vram_pending_free.emplace(mb, nowEpoch);
        }
    }
    else
    {
        VITA_DEBUG_LOG("[FFMPEG] vram_free: memblock %d not mapped -> free now", mb);
        sceKernelFreeMemBlock(mb);
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

    pic->buf[0] = av_buffer_create((uint8_t*)vram, size, vram_free, (void*)(intptr_t)mb, 0);
    if (!pic->buf[0])
    {
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
    vita2d_texture impl;
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
    AVBufferRef* buf = tex->frame.buf[0];
    if (!buf)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_dr_mutex);
    if (tex->vram_mapped && buf->data)
    {
        // Ensure GPU has finished using the texture before we unmap/free VRAM
        vita2d_wait_rendering_done();
        sceGxmUnmapMemory(buf->data);
        // adjust mapping count
        SceUID mb = (SceUID)(intptr_t)av_buffer_get_opaque(buf);
        if (mb != 0)
        {
            std::lock_guard<std::mutex> lock(g_vram_mutex);
            auto it = g_vram_map_count.find(mb);
            if (it != g_vram_map_count.end())
            {
                it->second = it->second > 0 ? it->second - 1 : 0;
                VITA_DEBUG_LOG("[FFMPEG] dr_texture_detach: memblock %d new_map_count=%d", mb, it->second);
                if (it->second == 0)
                {
                    g_vram_map_count.erase(it);
                    // If there was a deferred free, only release when no decode is active.
                    if (g_vram_pending_free.count(mb))
                    {
                        bool decoderActive = (g_active_decodes.load(std::memory_order_acquire) > 0);
                        bool stopRequested = g_ffmpeg_stop_request.load(std::memory_order_acquire);
                        VITA_DEBUG_LOG("[FFMPEG] dr_texture_detach: keeping deferred memblock %d (decoderActive=%d stop_request=%d)",
                            mb,
                            decoderActive ? 1 : 0,
                            stopRequested ? 1 : 0);
                    }
                }
            }
        }
        tex->vram_mapped = false;
    }
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

    VITA_DEBUG_LOG("[FFMPEG] dr_texture_attach: buf->data=%p buf->size=%d", buf->data, buf->size);

    int width  = FFMAX(FFALIGN(frame->width, 16), 64);
    int height = FFMAX(FFALIGN(frame->height, 16), 64);

    std::lock_guard<std::mutex> lock(g_dr_mutex);
    // Map the VRAM region to GXM once for this frame.
    if (!tex->vram_mapped && buf && buf->data)
    {
        int mapRes = sceGxmMapMemory(buf->data, buf->size, SCE_GXM_MEMORY_ATTRIB_READ);
        if (mapRes < 0)
        {
            VITA_DEBUG_LOG("[FFMPEG] dr_texture_attach: sceGxmMapMemory failed: 0x%08X", mapRes);
            return;
        }
        tex->vram_mapped = true;
        // track mapping count by memblock id if available
        SceUID mb = (SceUID)(intptr_t)av_buffer_get_opaque(buf);
        if (mb != 0)
        {
            std::lock_guard<std::mutex> lock(g_vram_mutex);
            g_vram_map_count[mb]++;
        }
    }
    sceGxmTextureInitLinear(&tex->impl.gxm_tex, buf->data, spec->sce_format, width, height, 0);
    // Ensure GPU finished using the previous frame before we unref/free it.
    vita2d_wait_rendering_done();
    av_frame_unref(&tex->frame);
    // Keep a reference to the frame for DR texture without transferring ownership
    // from the decoder. This avoids freeing the underlying memblock while the
    // decoder still has an active ref and prevents use-after-free in the
    // codec's internal threads (e.g., loop_filter).
    if (av_frame_ref(&tex->frame, frame) < 0)
    {
        VITA_DEBUG_LOG("[FFMPEG] dr_texture_attach: av_frame_ref failed");
        // Leave tex->frame cleared; decoder still owns the frame
        return;
    }
    AVBufferRef* bufRef = tex->frame.buf[0];
    if (bufRef)
    {
        SceUID mb = (SceUID)(intptr_t)av_buffer_get_opaque(bufRef);
        VITA_DEBUG_LOG("[FFMPEG] dr_texture_attach: took ref on frame buf memblock=%d buf->data=%p size=%d", mb, bufRef->data, (int)bufRef->size);
    }
}
#endif // BOREALIS_USE_GXM

#ifndef BOREALIS_USE_GXM
static inline void process_pending_vram_frees_if_safe() { }
#endif

static void reset_global_slots()
{
    std::lock_guard<std::mutex> slotLock(g_frame_slots_mutex);
    frame_textures[0]   = nullptr;
    frame_textures[1]   = nullptr;
    frame_front_idx     = 0;
    frame_back_idx      = 0;
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

static int ensure_sw_texture(FFmpegVideoContext* ctx, int width, int height)
{
#ifndef BOREALIS_USE_GXM
    (void)ctx;
    (void)width;
    (void)height;
    return -1;
#else
    if (width <= 0 || height <= 0)
    {
        return -1;
    }

    if (ctx->sw_texture && ctx->sw_texture_width == width && ctx->sw_texture_height == height)
    {
        return 0;
    }

    if (ctx->sw_texture)
    {
        vita2d_free_texture(ctx->sw_texture);
        ctx->sw_texture = nullptr;
    }

    ctx->sw_texture = vita2d_create_empty_texture_format(width, height, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    if (!ctx->sw_texture)
    {
        return -1;
    }

    ctx->sw_texture_width  = width;
    ctx->sw_texture_height = height;
    ctx->sw_texture_stride = vita2d_texture_get_stride(ctx->sw_texture);

    void* dst = vita2d_texture_get_datap(ctx->sw_texture);
    if (dst)
    {
        memset(dst, 0, (size_t)ctx->sw_texture_stride * (size_t)height);
    }
    return 0;
#endif
}

#ifdef BOREALIS_USE_GXM
static bool publish_direct_frame(FFmpegVideoContext* ctx, AVFrame* frame)
{
    if (!frame || !frame->buf[0])
    {
        VITA_DEBUG_LOG("[FFMPEG] publish_direct_frame: null frame or buf[0]");
        return false;
    }

    VITA_DEBUG_LOG("[FFMPEG] publish_direct_frame: frame->buf[0]=%p", frame->buf[0]);
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

    // Implement double buffering for direct textures (like legacy) to ensure
    // NVG re-creates images when texture handle changes.
    dr_texture* texFront = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_front_idx]);
    dr_texture* texBack  = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_back_idx]);
    if (!texBack)
    {
        texBack = dr_texture_alloc();
        if (!texBack)
        {
            return false;
        }
        ctx->dr_textures[ctx->dr_back_idx] = texBack;
    }

    // Detach any previous mapping/data and attach the new frame into the back texture
    dr_texture_detach(texBack);
    dr_texture_attach(texBack, frame);

    // Swap front/back so front points to newly attached texture
    int tmp           = ctx->dr_front_idx;
    ctx->dr_front_idx = ctx->dr_back_idx;
    ctx->dr_back_idx  = tmp;

    texFront                         = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_front_idx]);
    ctx->current_frame.texture       = &texFront->impl;
    ctx->current_frame.width         = frame->width;
    ctx->current_frame.height        = frame->height;
    ctx->current_frame.has_frame     = true;
    ctx->current_frame.direct_memory = true;
    ctx->using_direct_memory         = true;
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
    bool verboseSwLog                = ((s_sw_log_counter++ % 180) == 0);
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

    if (ensure_sw_texture(ctx, frame->width, frame->height) < 0)
    {
        return false;
    }

    // Use the actual source pixel format reported by the decoder/frame.
    enum AVPixelFormat srcFmt = (enum AVPixelFormat)frame->format;
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
        SWS_FAST_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (!ctx->sws_context)
    {
        brls::Logger::error("[FFMPEG] Unable to create swscale context for format {}", frame->format);
        return false;
    }

    uint8_t* dst = (uint8_t*)vita2d_texture_get_datap(ctx->sw_texture);
    if (!dst)
    {
        return false;
    }

    const uint8_t* inData[4] = { frame->data[0], frame->data[1], frame->data[2], frame->data[3] };
    int inStride[4]          = { frame->linesize[0], frame->linesize[1], frame->linesize[2], frame->linesize[3] };
    uint8_t* outData[4]      = { dst, nullptr, nullptr, nullptr };
    int outStride[4]         = { ctx->sw_texture_stride, 0, 0, 0 };
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
            tmp_inStride[0], tmp_inStride[1], tmp_inStride[2], outStride[0], ctx->sw_texture_stride);
    }
    int swsRet = sws_scale(ctx->sws_context, (const uint8_t**)tmp_inData, tmp_inStride, 0, frame->height, outData, outStride);
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
            uint8_t* dstrow = dst + (size_t)yy * (size_t)ctx->sw_texture_stride;
            uint8_t* srcrow = rgba_tmp + (size_t)yy * (size_t)rgba_stride;
            memcpy(dstrow, srcrow, (size_t)rgba_stride);
        }
        av_free(rgba_tmp);
        rgba_tmp = nullptr;
    }

    ctx->current_frame.texture       = ctx->sw_texture;
    ctx->current_frame.width         = frame->width;
    ctx->current_frame.height        = frame->height;
    ctx->current_frame.has_frame     = true;
    ctx->current_frame.direct_memory = false;
    ctx->using_direct_memory         = false;
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
    if ((s_publish_log_counter++ % 240) == 0)
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
        frame_front_idx     = 0;
        frame_back_idx      = 0;
        single_frame_buffer = true;

        if (ctx->using_direct_memory)
        {
#ifdef BOREALIS_USE_GXM
            // Use dedicated dr_textures for double-buffering so NVG re-creates image ids
            dr_texture* front   = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_front_idx]);
            dr_texture* back    = reinterpret_cast<dr_texture*>(ctx->dr_textures[ctx->dr_back_idx]);
            frame_textures[0]   = front ? &front->impl : ctx->current_frame.texture;
            frame_textures[1]   = back ? &back->impl : ctx->current_frame.texture;
            frame_front_idx     = 0;
            frame_back_idx      = 1;
            single_frame_buffer = false;
#else
            frame_textures[0]   = ctx->current_frame.texture;
            frame_textures[1]   = ctx->current_frame.texture;
            frame_front_idx     = 0;
            frame_back_idx      = 0;
            single_frame_buffer = true;
#endif
        }
        else
        {
            frame_textures[0]   = ctx->current_frame.texture;
            frame_textures[1]   = ctx->current_frame.texture;
            frame_front_idx     = 0;
            frame_back_idx      = 0;
            single_frame_buffer = true;
        }
    }

    uint32_t texW  = ctx->current_frame.width > 0 ? (uint32_t)ctx->current_frame.width : image_scaling.texture_width;
    uint32_t texH  = ctx->current_frame.height > 0 ? (uint32_t)ctx->current_frame.height : image_scaling.texture_height;
    uint64_t ptsMs = ptsUs ? (ptsUs / 1000ULL) : monotonic_ms_local();

#ifdef BOREALIS_USE_GXM
    if (ctx->using_direct_memory)
    {
        // We rely on dr_texture_attach to map VRAM memory when the frame is
        // attached. Mapping it here again could lead to double-mapping which
        // later causes range check failures on unmap. Only wait for rendering
        // to be done to avoid race conditions.
        vita2d_wait_rendering_done();
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
    for (int i = 0; i < 2; ++i)
    {
        if (ctx->dr_textures[i])
        {
            dr_texture* tex = reinterpret_cast<dr_texture*>(ctx->dr_textures[i]);
            dr_texture_free(&tex);
            ctx->dr_textures[i] = nullptr;
        }
    }
#endif
    if (ctx->sw_texture)
    {
        vita2d_free_texture(ctx->sw_texture);
        ctx->sw_texture = nullptr;
    }
    if (ctx->sws_context)
    {
        sws_freeContext(ctx->sws_context);
        ctx->sws_context = nullptr;
    }
    if (ctx->decoder.initialized)
    {
        ffmpeg_decoder_destroy(&ctx->decoder);
    }
    memset(&ctx->decoder, 0, sizeof(ctx->decoder));
    memset(&ctx->current_frame, 0, sizeof(ctx->current_frame));
    ctx->sw_texture_width    = 0;
    ctx->sw_texture_height   = 0;
    ctx->sw_texture_stride   = 0;
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

        if (vita2d_inited)
        {
            vita2d_wait_rendering_done();
        }
        memset(context, 0, sizeof(*context));
        context->dr_textures[0]      = nullptr;
        context->dr_textures[1]      = nullptr;
        context->dr_front_idx        = 0;
        context->dr_back_idx         = 1;
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
        std::lock_guard<std::mutex> lock(g_ffmpeg_mutex);
        if (g_ffmpeg_context == context)
        {
            g_ffmpeg_context = nullptr;
        }
        ffmpeg_release_locked(context);
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
        if (vita2d_inited)
        {
            vita2d_wait_rendering_done();
        }
        process_pending_vram_frees_if_safe();
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

        ffmpeg_release_locked(context);
        if (ffmpeg_video_init(context, width, height, redrawRate) < 0)
        {
            brls::Logger::error("[FFMPEG] init failed");
            return -1;
        }

#ifdef BOREALIS_USE_GXM
        if (!vita2d_inited)
        {
            if (width > 960 || height > 544)
            {
                vita2d_init_advanced(8 * 1024 * 1024);
            }
            else
            {
                vita2d_init();
            }
            vita2d_inited = true;
            vita2d_set_vblank_wait(0);
        }
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
        std::lock_guard<std::mutex> lock(g_ffmpeg_mutex);
        if (g_ffmpeg_context)
        {
            ffmpeg_release_locked(g_ffmpeg_context);
            g_ffmpeg_context = nullptr;
        }
    }

    static int ffmpeg_video_submit_decode_unit(PDECODE_UNIT decodeUnit)
    {
        if (!decodeUnit)
        {
            VITA_DEBUG_LOG("[FFMPEG] submit_decode_unit: null decodeUnit");
            return DR_NEED_IDR;
        }

        bool verboseDecodeLog = ((g_ffmpeg_submit_counter % 180) == 0);
        if (verboseDecodeLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] submit_decode_unit: size=%d pts=%llu", decodeUnit->fullLength, decodeUnit->presentationTimeUs);
        }

        std::unique_lock<std::mutex> lock(g_ffmpeg_mutex);
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
        if (!context || !context->initialized || !context->decoder.initialized)
        {
            // The decode unit memory is owned by the depacketizer/submitter. Do not free here
            // (freeing it here caused double-free/data-abort). The caller of submitDecodeUnit
            // (reassembleFrame) will handle completion/freeing via LiCompleteVideoFrame.
            return DR_NEED_IDR;
        }

        AVCodecContext* avctx = context->decoder.avctx;
        AVPacket* pkt         = context->decoder.pkt;
        AVFrame* frame        = context->decoder.frame;
        lock.unlock();

        if (verboseDecodeLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] before drain loop");
        }
        while (true)
        {
            int drain = avcodec_receive_frame(avctx, frame);
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
            if (publish_frame(context, frame, context->last_pts_us))
            {
                av_frame_unref(frame);
            }
        }
        if (verboseDecodeLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] after drain loop");
            VITA_DEBUG_LOG("[FFMPEG] before av_new_packet size=%d", decodeUnit->fullLength);
            VITA_DEBUG_LOG("[FFMPEG] pkt=%p avctx=%p frame=%p", pkt, avctx, frame);
        }
        if (av_new_packet(pkt, decodeUnit->fullLength) < 0)
        {
            brls::Logger::error("[FFMPEG] av_new_packet failed size={}", decodeUnit->fullLength);
            // Do not free decodeUnit here; ownership belongs to the depacketizer.
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

        int sendRes = avcodec_send_packet(avctx, pkt);
        av_packet_unref(pkt);
        if (verboseDecodeLog)
        {
            VITA_DEBUG_LOG("[FFMPEG] after send_packet res=%d", sendRes);
        }
        if (sendRes < 0 && sendRes != AVERROR(EAGAIN))
        {
            brls::Logger::error("[FFMPEG] send_packet error=0x{:X}", sendRes);
            return DR_NEED_IDR;
        }

        vita_netopt_on_frame_seen(g_ffmpeg_frame_index);

        while (true)
        {
            int ret = avcodec_receive_frame(avctx, frame);
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
                // Do not free decodeUnit here; caller (depacketizer) handles freeing.
                return DR_NEED_IDR;
            }

            if (publish_frame(context, frame, decodeUnit->presentationTimeUs))
            {
                if (stats_start_ms == 0)
                {
                    stats_start_ms = monotonic_ms_local();
                }
                g_stats.frames_decoded++;
                frame_count++;
                vita_netopt_frame_produced();
                vita_netopt_on_frame_completed(g_ffmpeg_frame_index);
                g_ffmpeg_frame_index++;
            }
            av_frame_unref(frame);
        }

        g_ffmpeg_submit_counter++;
        g_decode_epoch.fetch_add(1, std::memory_order_acq_rel);
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
        callbacks.capabilities               = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SLICES_PER_FRAME(2);
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
