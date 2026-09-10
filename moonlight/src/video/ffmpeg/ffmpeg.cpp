#include "ffmpeg.hpp"

#include <Limelight.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <climits>
#include <mutex>

#include "debug.hpp"
#include "gamestream/client.h"
#include "gamestream/errors.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

// Limelight button macros must be undefined before Borealis declares ControllerButton.
// clang-format off
#include "legacy/modules/vita_globals.hpp"
#include <borealis/core/application.hpp>
// clang-format on

#include "network/NetworkOptimizations.hpp"
#include "session/vita_session.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/runtime_settings.hpp"

#ifdef BOREALIS_USE_GXM
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/videodec.h>
#endif

extern "C" void vita_video_frame_published(void);

static FFmpegVideoContext* g_ffmpeg_context = nullptr;
static std::mutex g_ffmpeg_mutex;
static std::atomic<int> g_active_decodes { 0 };
static std::atomic<bool> g_ffmpeg_stop_request { false };
static std::atomic<bool> g_ffmpeg_watchdog_active { false };
static std::atomic<bool> g_ffmpeg_watchdog_fired { false };
static std::atomic<bool> g_ffmpeg_last_input_was_idr { false };
static std::atomic<uint32_t> g_ffmpeg_last_input_ms { 0 };
static uint64_t g_ffmpeg_last_input_callback_us = 0;
static uint64_t g_ffmpeg_last_enqueue_us        = 0;
static RTP_VIDEO_STATS g_ffmpeg_rtp_snapshot    = {};
static bool g_ffmpeg_rtp_snapshot_valid         = false;
static std::atomic<uint32_t> g_dr_pool_allocated { 0 };
static std::atomic<uint32_t> g_dr_pool_in_use { 0 };
static std::atomic<uint32_t> g_dr_pool_pending { 0 };
static std::atomic<uint32_t> g_dr_pool_exhaustions { 0 };
static std::atomic<uint32_t> g_dr_pool_alloc_failures { 0 };
static std::atomic<uint32_t> g_dr_pool_map_failures { 0 };

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
    uint32_t dropped_stale_frames;
    uint32_t stale_refreshes;
    uint32_t ingress_samples;
    uint64_t assembly_total_us;
    uint32_t assembly_max_us;
    uint64_t callback_delay_total_us;
    uint32_t callback_delay_max_us;
    uint32_t callback_gap_samples;
    uint64_t callback_gap_total_us;
    uint32_t callback_gap_max_us;
    uint32_t callback_gap_over_50ms;
    uint32_t enqueue_gap_samples;
    uint64_t enqueue_gap_total_us;
    uint32_t enqueue_gap_max_us;
    uint32_t enqueue_gap_over_50ms;
    uint64_t host_latency_total_us;
    uint32_t host_latency_max_us;
    uint32_t output_age_samples;
    uint64_t output_age_total_us;
    uint32_t output_age_max_us;
    uint64_t publish_total_us;
    uint32_t publish_max_us;
    uint64_t publish_direct_total_us;
    uint32_t publish_direct_max_us;
    uint64_t publish_sw_total_us;
    uint32_t publish_sw_max_us;
    uint64_t slots_mutex_total_us;
    uint32_t slots_mutex_max_us;
};

static ffmpeg_perf_counters g_perf = { 0 };

struct ffmpeg_perf_report
{
    ffmpeg_perf_counters perf;
    uint32_t elapsed_ms;
    uint32_t present_fps;
    int pending_frames;
    uint32_t pool_in_use;
    uint32_t pool_allocated;
    uint32_t pool_pending;
    uint32_t pool_exhaustions;
    uint32_t pool_alloc_failures;
    uint32_t pool_map_failures;
    bool has_net_delta;
    uint32_t net_packets;
    uint32_t net_fec;
    uint32_t net_recovered;
    uint32_t net_failed;
    uint32_t net_oos;
    uint32_t net_invalid;
    uint32_t net_fec_invalid;
};

constexpr uint32_t PERF_REPORT_QUEUE_CAPACITY = 4;
constexpr int PERF_REPORT_THREAD_PRIORITY     = 0x10000114;
static ffmpeg_perf_report g_perf_report_queue[PERF_REPORT_QUEUE_CAPACITY];
static std::atomic<uint32_t> g_perf_report_read { 0 };
static std::atomic<uint32_t> g_perf_report_write { 0 };
static std::atomic<uint32_t> g_perf_report_dropped { 0 };
static std::atomic<SceUID> g_perf_report_sema { -1 };
static std::atomic<SceUID> g_perf_report_thread { -1 };
static std::atomic<bool> g_perf_report_running { false };
static std::atomic<bool> g_perf_report_stop { false };
static std::mutex g_perf_report_lifecycle_mutex;

static inline uint32_t perf_now_us()
{
    return sceKernelGetProcessTimeLow();
}

static void write_perf_report(const ffmpeg_perf_report& report)
{
    const ffmpeg_perf_counters& perf = report.perf;
    uint32_t denom                   = report.elapsed_ms ? report.elapsed_ms : 1;
    uint32_t submitFps               = (uint32_t)(((uint64_t)perf.submit_calls * 1000ULL) / denom);
    uint32_t decodedFps              = (uint32_t)(((uint64_t)perf.decoded_frames * 1000ULL) / denom);
    uint32_t publishedFps            = (uint32_t)(((uint64_t)perf.published_frames * 1000ULL) / denom);
    uint32_t swsAvgUs                = perf.sws_calls ? (uint32_t)(perf.sws_total_us / perf.sws_calls) : 0;
    uint32_t submitAvgUs             = perf.submit_calls ? (uint32_t)(perf.submit_total_us / perf.submit_calls) : 0;
    uint32_t lockAvgUs               = perf.submit_calls ? (uint32_t)(perf.lock_wait_total_us / perf.submit_calls) : 0;
    uint32_t drainAvgUs              = perf.submit_calls ? (uint32_t)(perf.drain_total_us / perf.submit_calls) : 0;
    uint32_t copyAvgUs               = perf.submit_calls ? (uint32_t)(perf.copy_total_us / perf.submit_calls) : 0;
    uint32_t sendAvgUs               = perf.submit_calls ? (uint32_t)(perf.send_total_us / perf.submit_calls) : 0;
    uint32_t recvAvgUs               = perf.submit_calls ? (uint32_t)(perf.recv_total_us / perf.submit_calls) : 0;
    uint32_t publishAvgUs            = perf.published_frames ? (uint32_t)(perf.publish_total_us / perf.published_frames) : 0;
    uint32_t pubDirectAvgUs          = perf.published_frames ? (uint32_t)(perf.publish_direct_total_us / perf.published_frames) : 0;
    uint32_t pubSwAvgUs              = perf.published_frames ? (uint32_t)(perf.publish_sw_total_us / perf.published_frames) : 0;
    uint32_t slotsMtxAvgUs           = perf.published_frames ? (uint32_t)(perf.slots_mutex_total_us / perf.published_frames) : 0;
    uint32_t assemblyAvgUs           = perf.ingress_samples ? (uint32_t)(perf.assembly_total_us / perf.ingress_samples) : 0;
    uint32_t callbackAvgUs           = perf.ingress_samples ? (uint32_t)(perf.callback_delay_total_us / perf.ingress_samples) : 0;
    uint32_t callbackGapAvgUs        = perf.callback_gap_samples ? (uint32_t)(perf.callback_gap_total_us / perf.callback_gap_samples) : 0;
    uint32_t enqueueGapAvgUs         = perf.enqueue_gap_samples ? (uint32_t)(perf.enqueue_gap_total_us / perf.enqueue_gap_samples) : 0;
    uint32_t hostAvgUs               = perf.ingress_samples ? (uint32_t)(perf.host_latency_total_us / perf.ingress_samples) : 0;
    uint32_t outputAgeAvgUs          = perf.output_age_samples ? (uint32_t)(perf.output_age_total_us / perf.output_age_samples) : 0;

    VITA_DEBUG_LOG("[PERF][VIDEO] win=%ums submit=%u/s dec=%u/s pub=%u/s present=%u/s queue=%d",
        report.elapsed_ms, submitFps, decodedFps, publishedFps,
        report.present_fps, report.pending_frames);
    VITA_DEBUG_LOG("[PERF][VIDEO] submit_avg=%u/%umax lock=%u/%umax drain=%u/%umax copy=%u/%umax send=%u/%umax recv=%u/%umax",
        submitAvgUs, perf.submit_max_us, lockAvgUs, perf.lock_wait_max_us,
        drainAvgUs, perf.drain_max_us, copyAvgUs, perf.copy_max_us,
        sendAvgUs, perf.send_max_us, recvAvgUs, perf.recv_max_us);
    VITA_DEBUG_LOG("[PERF][VIDEO] pub_avg=%u/%umax dr=%u/%umax sw=%u/%umax slots_mtx=%u/%umax sws=%u/%umax stale=%u refresh=%u",
        publishAvgUs, perf.publish_max_us,
        pubDirectAvgUs, perf.publish_direct_max_us,
        pubSwAvgUs, perf.publish_sw_max_us,
        slotsMtxAvgUs, perf.slots_mutex_max_us,
        swsAvgUs, perf.sws_max_us,
        perf.dropped_stale_frames, perf.stale_refreshes);
    VITA_DEBUG_LOG("[PERF][VIDEO] ingress assembly=%u/%umax callback=%u/%umax enqueue_gap=%u/%umax enqueue50=%u callback_gap=%u/%umax callback50=%u host=%u/%umax age=%u/%umax",
        assemblyAvgUs, perf.assembly_max_us,
        callbackAvgUs, perf.callback_delay_max_us,
        enqueueGapAvgUs, perf.enqueue_gap_max_us, perf.enqueue_gap_over_50ms,
        callbackGapAvgUs, perf.callback_gap_max_us, perf.callback_gap_over_50ms,
        hostAvgUs, perf.host_latency_max_us,
        outputAgeAvgUs, perf.output_age_max_us);
    VITA_DEBUG_LOG("[PERF][VIDEO] pool=%u/%u pending=%u exhaust=%u alloc_fail=%u map_fail=%u",
        report.pool_in_use, report.pool_allocated, report.pool_pending,
        report.pool_exhaustions, report.pool_alloc_failures, report.pool_map_failures);
    if (report.has_net_delta)
    {
        VITA_DEBUG_LOG("[PERF][NET] packets=%u fec=%u recovered=%u failed=%u oos=%u invalid=%u fec_invalid=%u",
            report.net_packets, report.net_fec, report.net_recovered,
            report.net_failed, report.net_oos, report.net_invalid,
            report.net_fec_invalid);
    }
}

static int perf_report_thread_main(SceSize, void*)
{
    while (true)
    {
        SceUID sema = g_perf_report_sema.load(std::memory_order_acquire);
        if (sema < 0)
        {
            break;
        }
        sceKernelWaitSema(sema, 1, nullptr);

        uint32_t read  = g_perf_report_read.load(std::memory_order_relaxed);
        uint32_t write = g_perf_report_write.load(std::memory_order_acquire);
        while (read != write)
        {
            write_perf_report(g_perf_report_queue[read]);
            read = (read + 1) % PERF_REPORT_QUEUE_CAPACITY;
            g_perf_report_read.store(read, std::memory_order_release);
            write = g_perf_report_write.load(std::memory_order_acquire);
        }

        uint32_t dropped = g_perf_report_dropped.exchange(0, std::memory_order_acq_rel);
        if (dropped)
        {
            VITA_DEBUG_LOG("[PERF][VIDEO] reporter dropped %u snapshots", dropped);
        }
        if (g_perf_report_stop.load(std::memory_order_acquire) && read == write)
        {
            break;
        }
    }
    return 0;
}

static void stop_perf_reporter();

static void start_perf_reporter()
{
    std::lock_guard<std::mutex> lock(g_perf_report_lifecycle_mutex);
    if (g_perf_report_running.load(std::memory_order_acquire))
    {
        return;
    }

    SceUID sema = sceKernelCreateSema("ffmpeg_perf_queue", 0, 0, 1, nullptr);
    if (sema < 0)
    {
        VITA_DEBUG_LOG("[PERF][VIDEO] reporter sema creation failed: 0x%08X", sema);
        return;
    }
    SceUID thread = sceKernelCreateThread("ffmpeg_perf", perf_report_thread_main,
        PERF_REPORT_THREAD_PRIORITY, 0x8000, 0, 0, nullptr);
    if (thread < 0)
    {
        VITA_DEBUG_LOG("[PERF][VIDEO] reporter thread creation failed: 0x%08X", thread);
        sceKernelDeleteSema(sema);
        return;
    }

    g_perf_report_read.store(0, std::memory_order_relaxed);
    g_perf_report_write.store(0, std::memory_order_relaxed);
    g_perf_report_dropped.store(0, std::memory_order_relaxed);
    g_perf_report_stop.store(false, std::memory_order_release);
    g_perf_report_sema.store(sema, std::memory_order_release);
    g_perf_report_thread.store(thread, std::memory_order_release);
    int startResult = sceKernelStartThread(thread, 0, nullptr);
    if (startResult < 0)
    {
        VITA_DEBUG_LOG("[PERF][VIDEO] reporter thread start failed: 0x%08X", startResult);
        g_perf_report_thread.store(-1, std::memory_order_release);
        g_perf_report_sema.store(-1, std::memory_order_release);
        sceKernelDeleteThread(thread);
        sceKernelDeleteSema(sema);
        return;
    }
    g_perf_report_running.store(true, std::memory_order_release);
    VITA_DEBUG_LOG("[PERF][VIDEO] reporter started");
}

static void stop_perf_reporter()
{
    std::lock_guard<std::mutex> lock(g_perf_report_lifecycle_mutex);
    if (!g_perf_report_running.load(std::memory_order_acquire))
    {
        return;
    }

    g_perf_report_stop.store(true, std::memory_order_release);
    SceUID sema = g_perf_report_sema.load(std::memory_order_acquire);
    if (sema >= 0)
    {
        sceKernelSignalSema(sema, 1);
    }
    SceUID thread = g_perf_report_thread.load(std::memory_order_acquire);
    if (thread >= 0)
    {
        sceKernelWaitThreadEnd(thread, nullptr, nullptr);
        sceKernelDeleteThread(thread);
    }
    if (sema >= 0)
    {
        sceKernelDeleteSema(sema);
    }

    g_perf_report_thread.store(-1, std::memory_order_release);
    g_perf_report_sema.store(-1, std::memory_order_release);
    g_perf_report_running.store(false, std::memory_order_release);
    g_perf_report_stop.store(false, std::memory_order_release);
}

static void enqueue_perf_report(const ffmpeg_perf_report& report)
{
    if (!g_perf_report_running.load(std::memory_order_acquire))
    {
        return;
    }
    uint32_t write = g_perf_report_write.load(std::memory_order_relaxed);
    uint32_t next  = (write + 1) % PERF_REPORT_QUEUE_CAPACITY;
    if (next == g_perf_report_read.load(std::memory_order_acquire))
    {
        g_perf_report_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_perf_report_queue[write] = report;
    g_perf_report_write.store(next, std::memory_order_release);
    SceUID sema = g_perf_report_sema.load(std::memory_order_acquire);
    if (sema >= 0)
    {
        sceKernelSignalSema(sema, 1);
    }
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

    ffmpeg_perf_report report  = {};
    report.perf                = g_perf;
    report.elapsed_ms          = elapsedMs;
    report.present_fps         = video_stats::load(g_stats.current_fps);
    report.pending_frames      = LiGetPendingVideoFrames();
    report.pool_in_use         = g_dr_pool_in_use.load(std::memory_order_relaxed);
    report.pool_allocated      = g_dr_pool_allocated.load(std::memory_order_relaxed);
    report.pool_pending        = g_dr_pool_pending.load(std::memory_order_relaxed);
    report.pool_exhaustions    = g_dr_pool_exhaustions.exchange(0, std::memory_order_relaxed);
    report.pool_alloc_failures = g_dr_pool_alloc_failures.exchange(0, std::memory_order_relaxed);
    report.pool_map_failures   = g_dr_pool_map_failures.exchange(0, std::memory_order_relaxed);

    const RTP_VIDEO_STATS* rtpStats = LiGetRTPVideoStats();
    if (rtpStats)
    {
        if (g_ffmpeg_rtp_snapshot_valid)
        {
            report.has_net_delta   = true;
            report.net_packets     = rtpStats->packetCountVideo - g_ffmpeg_rtp_snapshot.packetCountVideo;
            report.net_fec         = rtpStats->packetCountFec - g_ffmpeg_rtp_snapshot.packetCountFec;
            report.net_recovered   = rtpStats->packetCountFecRecovered - g_ffmpeg_rtp_snapshot.packetCountFecRecovered;
            report.net_failed      = rtpStats->packetCountFecFailed - g_ffmpeg_rtp_snapshot.packetCountFecFailed;
            report.net_oos         = rtpStats->packetCountOOS - g_ffmpeg_rtp_snapshot.packetCountOOS;
            report.net_invalid     = rtpStats->packetCountInvalid - g_ffmpeg_rtp_snapshot.packetCountInvalid;
            report.net_fec_invalid = rtpStats->packetCountFecInvalid - g_ffmpeg_rtp_snapshot.packetCountFecInvalid;
        }
        g_ffmpeg_rtp_snapshot       = *rtpStats;
        g_ffmpeg_rtp_snapshot_valid = true;
    }

    memset(&g_perf, 0, sizeof(g_perf));
    g_perf.window_start_ms = nowMs;
    enqueue_perf_report(report);
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
static std::atomic<uint32_t> g_ffmpeg_presented_frames { 0 };

static constexpr int kDirectBufferPoolSize = 12;

struct DirectBufferSlot
{
    void* data;
    SceUID memblock;
    int size;
    bool in_use;
    bool pending_release;
    bool presented;
    uint32_t release_frame;
};

static DirectBufferSlot g_dr_pool[kDirectBufferPoolSize] = {};
static std::mutex g_dr_pool_mutex;

extern "C" void ffmpeg_increment_presented_frames(void)
{
    g_ffmpeg_presented_frames.fetch_add(1, std::memory_order_release);
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

static void reclaim_direct_buffers_locked(bool force)
{
    uint32_t currentPresented = g_ffmpeg_presented_frames.load(std::memory_order_acquire);
    for (DirectBufferSlot& slot : g_dr_pool)
    {
        if (!slot.pending_release)
        {
            continue;
        }
        if (force || currentPresented >= slot.release_frame + 3)
        {
            slot.pending_release = false;
            g_dr_pool_pending.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

static void release_direct_buffer(void* opaque, uint8_t* data)
{
    (void)data;
    DirectBufferSlot* slot = static_cast<DirectBufferSlot*>(opaque);
    if (!slot)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_dr_pool_mutex);
    if (!slot->in_use)
    {
        return;
    }
    slot->in_use = false;
    g_dr_pool_in_use.fetch_sub(1, std::memory_order_relaxed);
    if (slot->presented)
    {
        slot->pending_release = true;
        slot->release_frame   = g_ffmpeg_presented_frames.load(std::memory_order_acquire);
        g_dr_pool_pending.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        slot->pending_release = false;
    }
    slot->presented = false;
}

extern "C" void ffmpeg_mark_direct_buffer_presented(const void* data)
{
    if (!data)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_dr_pool_mutex);
    for (DirectBufferSlot& slot : g_dr_pool)
    {
        if (slot.in_use && slot.data == data)
        {
            slot.presented = true;
            return;
        }
    }
}

extern "C" void ffmpeg_process_deferred_releases(void)
{
    std::lock_guard<std::mutex> lock(g_dr_pool_mutex);
    reclaim_direct_buffers_locked(false);
}

static inline void process_pending_vram_frees_if_safe()
{
    ffmpeg_process_deferred_releases();
}

extern "C" void ffmpeg_flush_deferred_releases(void)
{
    std::lock_guard<std::mutex> lock(g_dr_pool_mutex);
    reclaim_direct_buffers_locked(true);
}

static void destroy_direct_buffer_pool()
{
    std::lock_guard<std::mutex> lock(g_dr_pool_mutex);
    reclaim_direct_buffers_locked(true);

    uint32_t remaining = 0;
    for (DirectBufferSlot& slot : g_dr_pool)
    {
        if (slot.in_use)
        {
            VITA_DEBUG_LOG("[FFMPEG][POOL] retaining in-use memblock %d during cleanup", slot.memblock);
            remaining++;
            continue;
        }
        if (slot.data)
        {
            sceGxmUnmapMemory(slot.data);
        }
        if (slot.memblock > 0)
        {
            sceKernelFreeMemBlock(slot.memblock);
        }
        memset(&slot, 0, sizeof(slot));
    }

    g_dr_pool_allocated.store(remaining, std::memory_order_relaxed);
    g_dr_pool_in_use.store(remaining, std::memory_order_relaxed);
    g_dr_pool_pending.store(0, std::memory_order_relaxed);
}

static DirectBufferSlot* acquire_direct_buffer(AVCodecContext* avctx, int size)
{
    std::lock_guard<std::mutex> lock(g_dr_pool_mutex);
    reclaim_direct_buffers_locked(false);

    for (DirectBufferSlot& slot : g_dr_pool)
    {
        if (slot.memblock > 0 && !slot.in_use && !slot.pending_release && slot.size >= size)
        {
            slot.in_use    = true;
            slot.presented = false;
            g_dr_pool_in_use.fetch_add(1, std::memory_order_relaxed);
            return &slot;
        }
    }

    for (DirectBufferSlot& slot : g_dr_pool)
    {
        if (slot.memblock > 0)
        {
            continue;
        }

        int allocationSize = size;
        if (!vram_alloc(&allocationSize, &slot.memblock, &slot.data))
        {
            g_dr_pool_alloc_failures.fetch_add(1, std::memory_order_relaxed);
            av_log(avctx, AV_LOG_ERROR, "vita direct buffer allocation failed for %d bytes\n", allocationSize);
            return nullptr;
        }

        int mapRes = sceGxmMapMemory(slot.data, allocationSize, SCE_GXM_MEMORY_ATTRIB_READ);
        if (mapRes < 0)
        {
            sceKernelFreeMemBlock(slot.memblock);
            memset(&slot, 0, sizeof(slot));
            g_dr_pool_map_failures.fetch_add(1, std::memory_order_relaxed);
            av_log(avctx, AV_LOG_ERROR, "vita direct buffer map failed: 0x%x\n", mapRes);
            return nullptr;
        }

        slot.size      = allocationSize;
        slot.in_use    = true;
        slot.presented = false;
        g_dr_pool_allocated.fetch_add(1, std::memory_order_relaxed);
        g_dr_pool_in_use.fetch_add(1, std::memory_order_relaxed);
        return &slot;
    }

    g_dr_pool_exhaustions.fetch_add(1, std::memory_order_relaxed);
    av_log(avctx, AV_LOG_ERROR, "vita direct buffer pool exhausted (%d slots)\n", kDirectBufferPoolSize);
    return nullptr;
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

    int size = av_image_get_buffer_size((enum AVPixelFormat)pic->format, pitch, height, 1);
    if (size < 0)
    {
        return size;
    }

    size                   = FFALIGN(size, 256 * 1024);
    DirectBufferSlot* slot = acquire_direct_buffer(avctx, size);
    if (!slot)
    {
        return AVERROR(ENOMEM);
    }

    int fillRes = av_image_fill_arrays(pic->data,
        pic->linesize,
        static_cast<const uint8_t*>(slot->data),
        (enum AVPixelFormat)pic->format,
        pitch,
        height,
        1);
    if (fillRes < 0)
    {
        std::lock_guard<std::mutex> lock(g_dr_pool_mutex);
        slot->in_use = false;
        g_dr_pool_in_use.fetch_sub(1, std::memory_order_relaxed);
        return fillRes;
    }

    pic->buf[0] = av_buffer_create(static_cast<uint8_t*>(slot->data), slot->size, release_direct_buffer, slot, 0);
    if (!pic->buf[0])
    {
        std::lock_guard<std::mutex> lock(g_dr_pool_mutex);
        slot->in_use = false;
        g_dr_pool_in_use.fetch_sub(1, std::memory_order_relaxed);
        return AVERROR(ENOMEM);
    }
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

static bool dr_texture_attach(dr_texture* tex, AVFrame* frame)
{
    if (!tex || !frame)
    {
        return false;
    }

    const dr_format_spec* spec = get_dr_format_spec((enum AVPixelFormat)frame->format);
    if (!spec)
    {
        return false;
    }

    AVBufferRef* buf = frame->buf[0];
    if (!buf)
    {
        return false;
    }

    int width  = FFMAX(FFALIGN(frame->width, 16), 64);
    int height = FFMAX(FFALIGN(frame->height, 16), 64);

    std::lock_guard<std::mutex> lock(g_dr_mutex);
    if (sceGxmTextureInitLinear(&tex->impl.gxm_tex, buf->data, spec->sce_format, width, height, 0) < 0)
    {
        return false;
    }
    tex->impl.width          = (uint32_t)frame->width;
    tex->impl.height         = (uint32_t)frame->height;
    tex->impl.storage_width  = (uint32_t)width;
    tex->impl.storage_height = (uint32_t)height;
    tex->impl.stride         = (uint32_t)frame->linesize[0];
    tex->impl.data_size      = (uint32_t)buf->size;
    tex->impl.format         = spec->sce_format;
    av_frame_unref(&tex->frame);
    // Keep a reference to the frame for DR texture without transferring ownership
    // from the decoder. This avoids freeing the underlying memblock while the
    // decoder still has an active ref and prevents use-after-free in the
    // codec's internal threads (e.g., loop_filter).
    if (av_frame_ref(&tex->frame, frame) < 0)
    {
        return false;
    }
    if (g_perf.published_frames == 0)
    {
        VITA_DEBUG_LOG("[FFMPEG][DR] texture visible=%dx%d storage=%dx%d pitch=%d",
            frame->width, frame->height, width, height, frame->linesize[0]);
    }
    return true;
}
#endif // BOREALIS_USE_GXM

static void reset_global_slots()
{
    std::lock_guard<std::mutex> slotLock(g_frame_slots_mutex);
    frame_textures[0] = nullptr;
    frame_textures[1] = nullptr;
    frame_textures[2] = nullptr;
    frame_display_idx = 0;
    frame_write_idx   = 1;
}

static uint32_t clamp_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static void record_ingress_timing(PDECODE_UNIT decodeUnit, uint64_t callbackTimeUs)
{
    if (!decodeUnit)
    {
        return;
    }

    uint32_t assemblyUs = 0;
    if (decodeUnit->enqueueTimeUs >= decodeUnit->receiveTimeUs)
    {
        assemblyUs = clamp_u32(decodeUnit->enqueueTimeUs - decodeUnit->receiveTimeUs);
    }

    uint32_t callbackDelayUs = 0;
    if (callbackTimeUs >= decodeUnit->enqueueTimeUs)
    {
        callbackDelayUs = clamp_u32(callbackTimeUs - decodeUnit->enqueueTimeUs);
    }

    if (g_ffmpeg_last_enqueue_us && decodeUnit->enqueueTimeUs >= g_ffmpeg_last_enqueue_us)
    {
        uint32_t enqueueGapUs = clamp_u32(decodeUnit->enqueueTimeUs - g_ffmpeg_last_enqueue_us);
        g_perf.enqueue_gap_samples++;
        g_perf.enqueue_gap_total_us += enqueueGapUs;
        if (enqueueGapUs > g_perf.enqueue_gap_max_us)
            g_perf.enqueue_gap_max_us = enqueueGapUs;
        if (enqueueGapUs > 50000U)
            g_perf.enqueue_gap_over_50ms++;
    }
    if (decodeUnit->enqueueTimeUs)
    {
        g_ffmpeg_last_enqueue_us = decodeUnit->enqueueTimeUs;
    }

    if (g_ffmpeg_last_input_callback_us && callbackTimeUs >= g_ffmpeg_last_input_callback_us)
    {
        uint32_t callbackGapUs = clamp_u32(callbackTimeUs - g_ffmpeg_last_input_callback_us);
        g_perf.callback_gap_samples++;
        g_perf.callback_gap_total_us += callbackGapUs;
        if (callbackGapUs > g_perf.callback_gap_max_us)
            g_perf.callback_gap_max_us = callbackGapUs;
        if (callbackGapUs > 50000U)
            g_perf.callback_gap_over_50ms++;
    }
    g_ffmpeg_last_input_callback_us = callbackTimeUs;
    g_ffmpeg_last_input_ms.store((uint32_t)(callbackTimeUs / 1000ULL), std::memory_order_release);
    g_ffmpeg_last_input_was_idr.store(decodeUnit->frameType == FRAME_TYPE_IDR, std::memory_order_release);
    g_ffmpeg_watchdog_fired.store(false, std::memory_order_release);

    uint32_t hostLatencyUs = (uint32_t)decodeUnit->frameHostProcessingLatency * 100U;
    g_perf.ingress_samples++;
    g_perf.assembly_total_us += assemblyUs;
    if (assemblyUs > g_perf.assembly_max_us)
        g_perf.assembly_max_us = assemblyUs;
    g_perf.callback_delay_total_us += callbackDelayUs;
    if (callbackDelayUs > g_perf.callback_delay_max_us)
        g_perf.callback_delay_max_us = callbackDelayUs;
    g_perf.host_latency_total_us += hostLatencyUs;
    if (hostLatencyUs > g_perf.host_latency_max_us)
        g_perf.host_latency_max_us = hostLatencyUs;
}

static void update_latency_epoch(FFmpegVideoContext* context, PDECODE_UNIT decodeUnit)
{
    if (!context || !decodeUnit || !decodeUnit->presentationTimeUs || decodeUnit->receiveTimeUs < decodeUnit->presentationTimeUs)
    {
        return;
    }

    if (context->latency_samples > 0 && (decodeUnit->frameNumber < context->last_input_frame_number || decodeUnit->presentationTimeUs < context->last_input_pts_us))
    {
        context->latency_epoch_offset_us = 0;
        context->latency_samples         = 0;
    }

    uint64_t offsetUs = decodeUnit->receiveTimeUs - decodeUnit->presentationTimeUs;
    if (context->latency_samples == 0 || offsetUs < context->latency_epoch_offset_us)
    {
        context->latency_epoch_offset_us = offsetUs;
    }
    context->last_input_frame_number = decodeUnit->frameNumber;
    context->last_input_pts_us       = decodeUnit->presentationTimeUs;
    context->latency_samples++;
}

static void reset_latency_tracking(FFmpegVideoContext* context)
{
    context->last_pts_us             = 0;
    context->latency_epoch_offset_us = 0;
    context->last_input_pts_us       = 0;
    context->last_input_frame_number = 0;
    context->latency_samples         = 0;
    context->output_age_floor_us     = 0;
    context->output_age_floor_valid  = false;
}

static uint32_t frame_interval_us()
{
    uint32_t targetFps = video_stats::load(g_stats.target_fps);
    if (targetFps == 0)
        targetFps = 60U;
    return 1000000U / targetFps;
}

static bool get_stream_age_us(FFmpegVideoContext* context, uint64_t ptsUs, uint32_t* ageUs)
{
    if (!context || !ptsUs || !ageUs || context->latency_samples < 4)
    {
        return false;
    }

    uint64_t expectedUs = context->latency_epoch_offset_us + ptsUs;
    uint64_t nowUs      = LiGetMicroseconds();
    if (nowUs <= expectedUs)
    {
        *ageUs = 0;
        return true;
    }

    *ageUs = clamp_u32(nowUs - expectedUs);
    return true;
}

static bool should_drop_stale_output(FFmpegVideoContext* context, uint64_t ptsUs)
{
    uint32_t ageUs = 0;
    if (!get_stream_age_us(context, ptsUs, &ageUs))
    {
        return false;
    }

    g_perf.output_age_samples++;
    g_perf.output_age_total_us += ageUs;
    if (ageUs > g_perf.output_age_max_us)
        g_perf.output_age_max_us = ageUs;

    if (!context->output_age_floor_valid || ageUs < context->output_age_floor_us)
    {
        context->output_age_floor_us    = ageUs;
        context->output_age_floor_valid = true;
    }
    uint32_t excessAgeUs = ageUs - context->output_age_floor_us;

    uint32_t staleThresholdUs = frame_interval_us() * 4U;
    if (excessAgeUs <= staleThresholdUs)
    {
        return false;
    }

    static uint32_t staleLogCounter = 0;
    if ((staleLogCounter++ % 30) == 0)
    {
        VITA_DEBUG_LOG("[FFMPEG][LAT] suppressing stale output age=%uus floor=%uus excess=%uus threshold=%uus pts=%llu",
            ageUs, context->output_age_floor_us, excessAgeUs,
            staleThresholdUs, (unsigned long long)ptsUs);
    }
    g_perf.dropped_stale_frames++;
    video_stats::increment(g_stats.frames_dropped_pacer);
    return true;
}

static bool should_refresh_stale_stream(FFmpegVideoContext* context, uint64_t ptsUs)
{
    if (!context || context->latency_samples < 8)
    {
        return false;
    }

    uint32_t ageUs = 0;
    if (!get_stream_age_us(context, ptsUs, &ageUs))
    {
        return false;
    }

    uint32_t refreshThresholdUs = frame_interval_us() * 12U;
    if (ageUs <= refreshThresholdUs)
    {
        return false;
    }

    VITA_DEBUG_LOG("[FFMPEG][LAT] abandoning stale stream age=%uus threshold=%uus; requesting IDR",
        ageUs, refreshThresholdUs);
    g_perf.dropped_stale_frames++;
    g_perf.stale_refreshes++;
    video_stats::increment(g_stats.frames_dropped_pacer);
    return true;
}

static int recover_decoder(FFmpegVideoContext* context, const char* stage, int error)
{
    char errorText[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(error, errorText, sizeof(errorText)) < 0)
    {
        snprintf(errorText, sizeof(errorText), "unknown error");
    }
    vita_log::error("[FFMPEG] %s error=0x%X (%s); reopening decoder", stage, error, errorText);
    context->decoder_resync_pending = false;
    ffmpeg_decoder_destroy(&context->decoder);
    process_pending_vram_frees_if_safe();
    if (ffmpeg_decoder_init(&context->decoder) < 0)
    {
        context->initialized = false;
        vita_log::error("[FFMPEG] decoder recovery failed");
    }
    else
    {
        context->decoder_prime_pending = true;
    }

    static uint64_t lastForcedIdrUs = 0;
    uint64_t nowUs                  = LiGetMicroseconds();
    if (!lastForcedIdrUs || nowUs - lastForcedIdrUs >= 100000ULL)
    {
        vita_netopt_force_idr();
        lastForcedIdrUs = nowUs;
    }
    return DR_NEED_IDR;
}

// Experimental YUV CSC fast-path is currently unstable with the active render path.
// Keep disabled by default until we have a dedicated YUV-safe presentation path.
// Runtime gated experimental path to bypass CPU sws conversion and upload YUV
// directly to CSC textures for GPU-side conversion/sampling.
static bool k_enable_yuv_csc_fast_path()
{
    return video_runtime::gpuYuvEnabled();
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
    VITA_DEBUG_LOG("[FFMPEG][SW] textures visible=%dx%d storage=%ux%u pitch=%u format=0x%08X",
        width, height,
        gxm_texture_get_storage_width(ctx->sw_textures[0]),
        gxm_texture_get_storage_height(ctx->sw_textures[0]),
        gxm_texture_get_stride(ctx->sw_textures[0]),
        (unsigned)textureFormat);
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
    if (!dr_texture_attach(texSpare, frame))
    {
        return false;
    }

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
            vita_log::error("[FFMPEG] Unable to create swscale context for format %d", frame->format);
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

        vita_log::error("[FFMPEG] sws_scale failed");
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
        return false;
    }

    if (!ctx->decoder_resync_pending)
    {
        if (should_refresh_stale_stream(ctx, ptsUs))
        {
            ctx->decoder_resync_pending = true;
            vita_netopt_force_idr();
            return false;
        }
        if (should_drop_stale_output(ctx, ptsUs))
        {
            return false;
        }
    }

    uint32_t publishStartUs = perf_now_us();

    bool published       = false;
    uint32_t pubDirectUs = 0, pubSwUs = 0;
#ifdef BOREALIS_USE_GXM
    if (ctx->decoder.use_direct_render)
    {
        uint32_t t0 = perf_now_us();
        published   = publish_direct_frame(ctx, frame);
        pubDirectUs = perf_now_us() - t0;
    }
#endif
    if (!published)
    {
        uint32_t t0 = perf_now_us();
        published   = publish_sw_frame(ctx, frame);
        pubSwUs     = perf_now_us() - t0;
    }

    if (!published || !ctx->current_frame.texture)
    {
        return false;
    }

    uint32_t slotsMtxStartUs = perf_now_us();
    {
        std::lock_guard<std::mutex> slotLock(g_frame_slots_mutex);

        // Keep a stable one-to-one texture mapping for NanoVG's image cache.
#ifdef BOREALIS_USE_GXM
        if (ctx->using_direct_memory)
        {
            for (int i = 0; i < 3; ++i)
            {
                dr_texture* tex   = reinterpret_cast<dr_texture*>(ctx->dr_textures[i]);
                GxmTexture* gxm_t = tex ? &tex->impl : nullptr;
                __atomic_store_n(&frame_textures[i], gxm_t, __ATOMIC_RELEASE);
            }
        }
        else
        {
            __atomic_store_n(&frame_textures[0], ctx->sw_textures[0], __ATOMIC_RELEASE);
            __atomic_store_n(&frame_textures[1], ctx->sw_textures[1], __ATOMIC_RELEASE);
            __atomic_store_n(&frame_textures[2], ctx->sw_textures[2], __ATOMIC_RELEASE);
        }
#else
        __atomic_store_n(&frame_textures[0], ctx->sw_textures[0], __ATOMIC_RELEASE);
        __atomic_store_n(&frame_textures[1], ctx->sw_textures[1], __ATOMIC_RELEASE);
        __atomic_store_n(&frame_textures[2], ctx->sw_textures[2], __ATOMIC_RELEASE);
#endif

        int active_idx = 0;
#ifdef BOREALIS_USE_GXM
        if (ctx->using_direct_memory)
        {
            active_idx = ctx->dr_front_idx;
        }
        else
        {
            active_idx = ctx->sw_last_present_idx;
        }
#else
        active_idx = ctx->sw_last_present_idx;
#endif

        __atomic_store_n(&frame_display_idx, active_idx, __ATOMIC_RELEASE);
        __atomic_store_n(&frame_publish_timestamp_us, perf_now_us(), __ATOMIC_RELEASE);
    }
    uint32_t slotsMtxUs = perf_now_us() - slotsMtxStartUs;

    // Wake the render loop now instead of letting it sleep out its frame budget.
    vita_video_frame_published();

    ctx->last_pts_us        = ptsUs;
    uint32_t publishTotalUs = perf_now_us() - publishStartUs;

    g_perf.publish_total_us += publishTotalUs;
    if (publishTotalUs > g_perf.publish_max_us)
        g_perf.publish_max_us = publishTotalUs;
    g_perf.publish_direct_total_us += pubDirectUs;
    if (pubDirectUs > g_perf.publish_direct_max_us)
        g_perf.publish_direct_max_us = pubDirectUs;
    g_perf.publish_sw_total_us += pubSwUs;
    if (pubSwUs > g_perf.publish_sw_max_us)
        g_perf.publish_sw_max_us = pubSwUs;
    g_perf.slots_mutex_total_us += slotsMtxUs;
    if (slotsMtxUs > g_perf.slots_mutex_max_us)
        g_perf.slots_mutex_max_us = slotsMtxUs;
    VitaSession::onFrameDecoded();
    return true;
}

static void ffmpeg_release_locked(FFmpegVideoContext* ctx)
{
    if (!ctx)
    {
        return;
    }
    stop_perf_reporter();

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
    destroy_direct_buffer_pool();
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

    if (g_sps_ctx)
    {
        delete g_sps_ctx;
        g_sps_ctx = nullptr;
        VITA_DEBUG_LOG("[FFMPEG][SPS] Contexto SPS destruido");
    }
}

extern "C"
{

    int ffmpeg_video_init(FFmpegVideoContext* context, int width, int height, int frame_rate)
    {
        if (!context)
        {
            return -1;
        }

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
        g_ffmpeg_watchdog_active.store(false, std::memory_order_release);
        g_ffmpeg_last_input_ms.store(0, std::memory_order_release);
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
        video_stats::store(g_stats.target_fps,
            context->frame_rate > 0 ? (uint32_t)context->frame_rate : 60);
        g_ffmpeg_watchdog_active.store(true, std::memory_order_release);
    }

    static void ffmpeg_video_stop_locked(FFmpegVideoContext* context)
    {
        if (!context)
            return;
        stop_perf_reporter();
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

        reset_global_slots();
        context->current_frame.texture       = nullptr;
        context->current_frame.width         = 0;
        context->current_frame.height        = 0;
        context->current_frame.has_frame     = false;
        context->current_frame.direct_memory = false;
        context->using_direct_memory         = false;
        process_pending_vram_frees_if_safe();
        ffmpeg_flush_deferred_releases();
    }

    void ffmpeg_video_stop(FFmpegVideoContext* context)
    {
        // Signal stop request: prevent new decodes from starting, and wait for in-flight
        // decodes to finish before destroying resources.
        g_ffmpeg_watchdog_active.store(false, std::memory_order_release);
        g_ffmpeg_last_input_ms.store(0, std::memory_order_release);
        g_ffmpeg_stop_request.store(true, std::memory_order_release);
        // Immediately clear any renderer-held references so the UI stops sampling
        // the soon-to-be-destroyed textures while we wait for the decoder to drain.
#ifdef BOREALIS_USE_GXM
        VitaVideoRenderer::instance().destroyImage(nullptr);
#endif
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
            vita_log::error("[FFMPEG] setup received null context");
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
        vita_log::info("[FFMPEG] setup %dx%d @ %dfps ctx=%p", width, height, redrawRate, context);

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
            vita_log::error("[FFMPEG] init failed");
            return -1;
        }

#ifdef BOREALIS_USE_GXM
#endif

        vitavideo_update_scaling_settings(width, height);
        video_fullscreen_stretch = video_runtime::fullscreen();

        if (!g_sps_ctx)
        {
            g_sps_ctx = new gs::SpsContext(width, height);
            if (g_sps_ctx)
            {
                VITA_DEBUG_LOG("[FFMPEG][SPS] Contexto SPS inicializado (%dx%d)", width, height);
            }
        }

        memset(&g_stats, 0, sizeof(g_stats));
        memset(&g_perf, 0, sizeof(g_perf));
        g_ffmpeg_last_input_callback_us = 0;
        g_ffmpeg_last_enqueue_us        = 0;
        g_ffmpeg_last_input_ms.store(0, std::memory_order_relaxed);
        g_ffmpeg_watchdog_fired.store(false, std::memory_order_relaxed);
        g_ffmpeg_last_input_was_idr.store(false, std::memory_order_relaxed);
        g_ffmpeg_watchdog_active.store(false, std::memory_order_relaxed);
        const RTP_VIDEO_STATS* rtpStats = LiGetRTPVideoStats();
        if (rtpStats)
        {
            g_ffmpeg_rtp_snapshot       = *rtpStats;
            g_ffmpeg_rtp_snapshot_valid = true;
        }
        else
        {
            g_ffmpeg_rtp_snapshot_valid = false;
        }
        g_dr_pool_exhaustions.store(0, std::memory_order_relaxed);
        g_dr_pool_alloc_failures.store(0, std::memory_order_relaxed);
        g_dr_pool_map_failures.store(0, std::memory_order_relaxed);
#ifdef BOREALIS_USE_GXM
        g_ffmpeg_presented_frames.store(0, std::memory_order_relaxed);
#endif
        stats_start_ms     = 0;
        last_fps_window_ms = 0;
        video_stats::store(g_stats.target_fps,
            redrawRate > 0 ? (uint32_t)redrawRate : 60);
        start_perf_reporter();

        if (ffmpeg_decoder_init(&context->decoder) < 0)
        {
            vita_log::error("[FFMPEG] decoder init failed");
            ffmpeg_release_locked(context);
            return -1;
        }
        context->decoder_prime_pending  = true;
        context->decoder_resync_pending = false;
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
        g_ffmpeg_watchdog_active.store(false, std::memory_order_release);
        g_ffmpeg_last_input_ms.store(0, std::memory_order_release);
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

    void ffmpeg_video_watchdog_tick(void)
    {
        if (!g_ffmpeg_watchdog_active.load(std::memory_order_acquire) || g_ffmpeg_stop_request.load(std::memory_order_acquire) || g_ffmpeg_last_input_was_idr.load(std::memory_order_acquire))
        {
            return;
        }

        uint32_t lastInputMs = g_ffmpeg_last_input_ms.load(std::memory_order_acquire);
        if (!lastInputMs)
        {
            return;
        }

        uint32_t nowMs  = (uint32_t)LiGetMillis();
        uint32_t idleMs = nowMs - lastInputMs;
        if (idleMs < 250U || g_ffmpeg_last_input_ms.load(std::memory_order_acquire) != lastInputMs)
        {
            return;
        }

        bool expected = false;
        if (!g_ffmpeg_watchdog_fired.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return;
        }

        const RTP_VIDEO_STATS* rtpStats = LiGetRTPVideoStats();
        if (rtpStats)
        {
            VITA_DEBUG_LOG("[FFMPEG][WATCHDOG] no input for %ums; requesting IDR (packets=%u recovered=%u failed=%u oos=%u invalid=%u)",
                idleMs,
                rtpStats->packetCountVideo,
                rtpStats->packetCountFecRecovered,
                rtpStats->packetCountFecFailed,
                rtpStats->packetCountOOS,
                rtpStats->packetCountInvalid);
        }
        else
        {
            VITA_DEBUG_LOG("[FFMPEG][WATCHDOG] no input for %ums; requesting IDR", idleMs);
        }
        vita_netopt_force_idr();
    }

    static int ffmpeg_video_submit_decode_unit(PDECODE_UNIT decodeUnit)
    {
        g_perf.submit_calls++;
        uint32_t submitStartUs  = perf_now_us();
        uint64_t callbackTimeUs = LiGetMicroseconds();
        record_ingress_timing(decodeUnit, callbackTimeUs);
        uint32_t lockWaitUs = 0;
        uint32_t drainUs    = 0;
        uint32_t copyUs     = 0;
        uint32_t sendUs     = 0;
        uint32_t recvUs     = 0;

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

        if (context->decoder_resync_pending && decodeUnit->frameType == FRAME_TYPE_IDR)
        {
            VITA_DEBUG_LOG("[FFMPEG][LAT] resync IDR received; flushing decoder before decode");
            avcodec_flush_buffers(avctx);
            reset_latency_tracking(context);
            context->decoder_resync_pending = false;
        }

        update_latency_epoch(context, decodeUnit);

        if (!context->decoder_resync_pending && should_refresh_stale_stream(context, decodeUnit->presentationTimeUs))
        {
            avcodec_flush_buffers(avctx);
            reset_latency_tracking(context);
            finalize_submit_metrics();
            return DR_NEED_IDR;
        }

        uint32_t drainStartUs         = perf_now_us();
        bool drainReceived            = false;
        static AVFrame* s_drain_frame = nullptr;
        if (!s_drain_frame)
            s_drain_frame = av_frame_alloc();
        if (s_drain_frame)
        {
            while (true)
            {
                int drain = avcodec_receive_frame(avctx, s_drain_frame);
                if (drain == AVERROR(EAGAIN) || drain == AVERROR_EOF)
                {
                    break;
                }
                if (drain < 0)
                {
                    drainUs = perf_now_us() - drainStartUs;
                    finalize_submit_metrics();
                    return recover_decoder(context, "receive_frame drain", drain);
                }
                if (drainReceived)
                {
                    av_frame_unref(frame);
                    g_perf.dropped_stale_frames++;
                }
                av_frame_move_ref(frame, s_drain_frame);
                drainReceived = true;
            }
        }
        else
        {
            drainUs = perf_now_us() - drainStartUs;
            finalize_submit_metrics();
            return recover_decoder(context, "allocate drain frame", AVERROR(ENOMEM));
        }
        if (drainReceived)
        {
            int64_t drainPts = (frame->pts != AV_NOPTS_VALUE)
                ? frame->pts
                : (context->last_pts_us ? (int64_t)context->last_pts_us
                                        : (int64_t)decodeUnit->presentationTimeUs);
            int64_t ptsDiff  = (int64_t)decodeUnit->presentationTimeUs - drainPts;
            if (ptsDiff != 0)
            {
                static uint32_t s_drain_pts_mismatch_counter = 0;
                if ((s_drain_pts_mismatch_counter++ % 60) == 0)
                {
                    VITA_DEBUG_LOG("[FFMPEG][DIAG] drained PTS distance: submitted=%lld frame=%lld diff=%lld us (%lld ms)",
                        (long long)decodeUnit->presentationTimeUs, (long long)drainPts,
                        (long long)ptsDiff, (long long)(ptsDiff / 1000));
                }
            }
            publish_frame(context, frame, static_cast<uint64_t>(drainPts));
            av_frame_unref(frame);
        }
        drainUs = perf_now_us() - drainStartUs;

        uint32_t copyStartUs = perf_now_us();
        if (decodeUnit->fullLength <= 0 || !decodeUnit->bufferList || decodeUnit->fullLength > INT_MAX - 256)
        {
            copyUs = perf_now_us() - copyStartUs;
            finalize_submit_metrics();
            return recover_decoder(context, "invalid decode unit", AVERROR_INVALIDDATA);
        }
        int packetRes = av_new_packet(pkt, decodeUnit->fullLength + 256);
        if (packetRes < 0)
        {
            vita_log::error("[FFMPEG] av_new_packet failed size=%d", decodeUnit->fullLength + 256);
            // Do not free decodeUnit here; ownership belongs to the depacketizer.
            copyUs = perf_now_us() - copyStartUs;
            finalize_submit_metrics();
            return recover_decoder(context, "allocate packet", packetRes);
        }
        uint8_t* dst                = pkt->data;
        size_t offset               = 0;
        const size_t packetCapacity = static_cast<size_t>(pkt->size);
        const auto appendEntry      = [&](PLENTRY current)
        {
            if (!current || !current->data || current->length <= 0 || offset > packetCapacity)
            {
                return false;
            }
            const size_t entryLength = static_cast<size_t>(current->length);
            if (entryLength > packetCapacity - offset)
            {
                return false;
            }
            memcpy(dst + offset, current->data, entryLength);
            offset += entryLength;
            return true;
        };

        PLENTRY entry = decodeUnit->bufferList;
        while (entry)
        {
            bool appended = false;
            if (entry->bufferType == BUFFER_TYPE_SPS)
            {
                if (g_sps_ctx)
                {
                    appended = g_sps_ctx->fix(entry, GS_SPS_BITSTREAM_FIXUP, dst,
                        packetCapacity, &offset);
                }
                else
                {
                    appended = appendEntry(entry);
                }
            }
            else
            {
                appended = appendEntry(entry);
            }
            if (!appended)
            {
                av_packet_unref(pkt);
                copyUs = perf_now_us() - copyStartUs;
                finalize_submit_metrics();
                return recover_decoder(context, "assemble packet", AVERROR_INVALIDDATA);
            }
            entry = entry->next;
        }
        av_shrink_packet(pkt, static_cast<int>(offset));
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
        if (sendRes < 0)
        {
            finalize_submit_metrics();
            return recover_decoder(context, "send_packet", sendRes);
        }
        if (context->decoder_prime_pending)
        {
            context->decoder_prime_pending   = false;
            uint32_t primeBacklogThresholdUs = frame_interval_us() * 4U;
            if (sendUs > primeBacklogThresholdUs)
            {
                VITA_DEBUG_LOG("[FFMPEG][LAT] decoder priming took %uus; requesting resync IDR",
                    sendUs);
                context->decoder_resync_pending = true;
                vita_netopt_force_idr();
            }
        }

        uint32_t recvStartUs         = perf_now_us();
        bool frameReceived           = false;
        static AVFrame* s_recv_frame = nullptr;
        if (!s_recv_frame)
            s_recv_frame = av_frame_alloc();
        if (s_recv_frame)
        {
            while (true)
            {
                int ret = avcodec_receive_frame(avctx, s_recv_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                if (ret < 0)
                {
                    // Do not free decodeUnit here; caller (depacketizer) handles freeing.
                    recvUs = perf_now_us() - recvStartUs;
                    finalize_submit_metrics();
                    return recover_decoder(context, "receive_frame", ret);
                }
                if (frameReceived)
                {
                    av_frame_unref(frame);
                    g_perf.dropped_stale_frames++;
                }
                av_frame_move_ref(frame, s_recv_frame);
                frameReceived = true;
            }
        }
        else
        {
            recvUs = perf_now_us() - recvStartUs;
            finalize_submit_metrics();
            return recover_decoder(context, "allocate receive frame", AVERROR(ENOMEM));
        }
        recvUs = perf_now_us() - recvStartUs;

        if (frameReceived)
        {
            uint32_t dec_ms = (sendUs + recvUs) / 1000;
            video_stats::add(g_stats.decode_time_ms, dec_ms);
            video_stats::updateMinimum(g_decode_min_ms, dec_ms);
            video_stats::updateMaximum(g_decode_max_ms, dec_ms);
            video_stats::add(g_decode_sum_ms, dec_ms);
            video_stats::increment(g_decode_count);

            int64_t framePts = (frame->pts != AV_NOPTS_VALUE) ? frame->pts : (int64_t)decodeUnit->presentationTimeUs;
            int64_t ptsDiff  = (int64_t)decodeUnit->presentationTimeUs - framePts;
            if (ptsDiff != 0)
            {
                static uint32_t s_pts_mismatch_counter = 0;
                if ((s_pts_mismatch_counter++ % 60) == 0)
                {
                    VITA_DEBUG_LOG("[FFMPEG][DIAG] PTS mismatch: submitted=%lld frame=%lld diff=%lld us (%lld ms)",
                        (long long)decodeUnit->presentationTimeUs, (long long)framePts,
                        (long long)ptsDiff, (long long)(ptsDiff / 1000));
                }
            }

            publish_frame(context, frame, static_cast<uint64_t>(framePts));
            video_stats::increment(g_stats.frames_decoded);
            g_perf.decoded_frames++;
            av_frame_unref(frame);
        }

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
        callbacks.capabilities               = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SLICES_PER_FRAME(2);
        VITA_DEBUG_LOG("[FFMPEG] Decode submission uses the network thread");
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
