#include "vita_globals.hpp"

#include <borealis/extern/nanovg/nanovg_gxm_utils.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>

#include <mutex>

#include "debug.hpp"
#include "gamestream/sps.h"
#include "video/runtime_settings.hpp"

VideoStatus video_status          = VITA_VIDEO_NOT_INIT;
uint32_t video_status_canary_pre  = 0xDEADBEEF;
uint32_t video_status_canary_post = 0xBAADF00D;

char* decoder_buffer                    = NULL;
size_t decoder_buffer_size              = 0;
SceAvcdecCtrl* decoder                  = NULL;
int decoderblock                        = -1;
size_t decoder_block_size               = 0;
SceAvcdecQueryDecoderInfo* decoder_info = NULL;
SceVideodecQueryInitInfoHwAvcdec* init  = NULL;

GxmTexture* frame_textures[3] = { nullptr, nullptr, nullptr };
int frame_display_idx         = 0; // GPU reads from this
int frame_write_idx           = 1; // decoder writes to this
std::mutex g_frame_slots_mutex;

namespace
{
constexpr int LOW_LATENCY_BACKLOG_THRESHOLD = 4;

uint32_t s_frame_generation              = 0;
uint32_t s_presented_generation          = 0;
uint32_t s_slot_generation[3]            = { 0, 0, 0 };
uint32_t s_slot_publish_timestamp_us[3]  = { 0, 0, 0 };
uint32_t s_slot_frame_number[3]          = { 0, 0, 0 };
uint32_t s_slot_notification_pending[3]  = { 0, 0, 0 };
uint32_t s_notification_value            = 0;
bool s_notification_initialized          = false;
volatile uint32_t* s_notification_region = nullptr;
bool s_backlog_refresh_pending           = false;
int s_waiting_slot                       = -1;
uint32_t s_waiting_value                 = 0;
uint32_t s_waiting_start_us              = 0;
uint32_t s_waiting_last_log_us           = 0;

volatile uint32_t* notification_address()
{
    if (!s_notification_region)
    {
        s_notification_region = sceGxmGetNotificationRegion();
    }
    return s_notification_region;
}

bool notification_reached(uint32_t current, uint32_t expected)
{
    return static_cast<int32_t>(current - expected) >= 0;
}

void arm_slot_notification(int slot)
{
    volatile uint32_t* address = notification_address();
    if (!address)
    {
        return;
    }

    if (!s_notification_initialized)
    {
        s_notification_value       = __atomic_load_n(address, __ATOMIC_ACQUIRE);
        s_notification_initialized = true;
    }

    uint32_t completed = __atomic_load_n(address, __ATOMIC_ACQUIRE);
    uint32_t value     = ++s_notification_value;
    if (value == 0)
    {
        value = ++s_notification_value;
    }

    // GXM accepts one fragment notification per scene, so the latest value
    // must also cover any other texture armed during the same scene.
    for (int i = 0; i < 3; ++i)
    {
        uint32_t pending = __atomic_load_n(&s_slot_notification_pending[i], __ATOMIC_RELAXED);
        if (pending != 0 && pending != completed)
        {
            __atomic_store_n(&s_slot_notification_pending[i], value, __ATOMIC_RELAXED);
        }
    }

    SceGxmNotification notification = { address, value };
    __atomic_store_n(&s_slot_notification_pending[slot], value, __ATOMIC_RELEASE);
    gxmSetFragmentNotification(&notification);
}
}

int SCREEN_WIDTH  = 960;
int SCREEN_HEIGHT = 544;

image_scaling_settings image_scaling = { 0 };

std::atomic<bool> active_video_thread { false };

bool video_fullscreen_stretch = true; // activated by default to occupy full screen
int decoder_output_mode       = 0; // 0=RGBA direct, 1=YUV420 GPU CSC
int decoder_width             = 0;
int decoder_height            = 0;

VitaVideoStats g_stats      = { 0 };
uint64_t stats_start_ms     = 0;
uint64_t last_fps_window_ms = 0;

gs::SpsContext* g_sps_ctx = nullptr; // SPS context active (SPS fix)

bool video_frame_acquire_for_render(VideoFrameSnapshot* snapshot)
{
    if (!snapshot)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_frame_slots_mutex);
    uint32_t generation = __atomic_load_n(&s_frame_generation, __ATOMIC_ACQUIRE);
    int slot            = __atomic_load_n(&frame_display_idx, __ATOMIC_RELAXED);
    if (generation == 0 || slot < 0 || slot >= 3)
    {
        return false;
    }

    GxmTexture* texture = __atomic_load_n(&frame_textures[slot], __ATOMIC_ACQUIRE);
    if (!texture || __atomic_load_n(&s_slot_generation[slot], __ATOMIC_ACQUIRE) != generation)
    {
        return false;
    }

    snapshot->texture              = texture;
    snapshot->generation           = generation;
    snapshot->publish_timestamp_us = __atomic_load_n(&s_slot_publish_timestamp_us[slot], __ATOMIC_RELAXED);
    snapshot->frame_number         = __atomic_load_n(&s_slot_frame_number[slot], __ATOMIC_RELAXED);
    snapshot->slot                 = slot;
    arm_slot_notification(slot);
    return true;
}

void video_frame_publish(int slot, uint32_t frameNumber, uint32_t publishTimestampUs)
{
    if (slot < 0 || slot >= 3)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_frame_slots_mutex);
    uint32_t previousGeneration  = __atomic_load_n(&s_frame_generation, __ATOMIC_RELAXED);
    uint32_t presentedGeneration = __atomic_load_n(&s_presented_generation, __ATOMIC_ACQUIRE);
    if (previousGeneration != 0 && previousGeneration != presentedGeneration)
    {
        video_stats::increment(g_stats.frames_dropped_pacer);
    }

    uint32_t generation = previousGeneration + 1;
    if (generation == 0)
    {
        generation = 1;
    }
    __atomic_store_n(&s_slot_publish_timestamp_us[slot], publishTimestampUs, __ATOMIC_RELAXED);
    __atomic_store_n(&s_slot_frame_number[slot], frameNumber, __ATOMIC_RELAXED);
    __atomic_store_n(&s_slot_generation[slot], generation, __ATOMIC_RELEASE);
    __atomic_store_n(&frame_display_idx, slot, __ATOMIC_RELAXED);
    __atomic_store_n(&s_frame_generation, generation, __ATOMIC_RELEASE);
}

void video_frame_wait_for_slot(int slot)
{
    if (slot < 0 || slot >= 3)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_frame_slots_mutex);
    uint32_t value             = __atomic_load_n(&s_slot_notification_pending[slot], __ATOMIC_ACQUIRE);
    volatile uint32_t* address = notification_address();
    uint32_t current           = address ? __atomic_load_n(address, __ATOMIC_ACQUIRE) : 0;
    if (value != 0 && address && !notification_reached(current, value))
    {
        uint32_t startUs = sceKernelGetProcessTimeLow();
        __atomic_store_n(&s_waiting_value, value, __ATOMIC_RELAXED);
        __atomic_store_n(&s_waiting_start_us, startUs, __ATOMIC_RELAXED);
        __atomic_store_n(&s_waiting_slot, slot, __ATOMIC_RELEASE);

        do
        {
            sceKernelDelayThread(250);
            current = __atomic_load_n(address, __ATOMIC_ACQUIRE);
        } while (!notification_reached(current, value));

        uint32_t elapsedUs = sceKernelGetProcessTimeLow() - startUs;
        __atomic_store_n(&s_waiting_slot, -1, __ATOMIC_RELEASE);
        if (elapsedUs >= 20000)
        {
            VITA_DEBUG_LOG("[Video][FENCE] wait completed slot=%d expected=%u current=%u elapsed=%u us",
                slot, value, current, elapsedUs);
        }
    }
}

void video_frame_watchdog_tick()
{
    int slot = __atomic_load_n(&s_waiting_slot, __ATOMIC_ACQUIRE);
    if (slot < 0 || slot >= 3)
    {
        return;
    }

    uint32_t nowUs     = sceKernelGetProcessTimeLow();
    uint32_t startUs   = __atomic_load_n(&s_waiting_start_us, __ATOMIC_RELAXED);
    uint32_t elapsedUs = nowUs - startUs;
    if (elapsedUs < 20000)
    {
        return;
    }

    uint32_t previousLogUs = __atomic_load_n(&s_waiting_last_log_us, __ATOMIC_RELAXED);
    if (previousLogUs != 0 && nowUs - previousLogUs < 250000)
    {
        return;
    }
    __atomic_store_n(&s_waiting_last_log_us, nowUs, __ATOMIC_RELAXED);

    volatile uint32_t* address = s_notification_region;
    uint32_t current           = address ? __atomic_load_n(address, __ATOMIC_ACQUIRE) : 0;
    VITA_DEBUG_LOG("[Video][FENCE] wait stalled slot=%d expected=%u current=%u elapsed=%u us display=%d write=%d generation=%u",
        slot,
        __atomic_load_n(&s_waiting_value, __ATOMIC_RELAXED),
        current,
        elapsedUs,
        __atomic_load_n(&frame_display_idx, __ATOMIC_RELAXED),
        __atomic_load_n(&frame_write_idx, __ATOMIC_RELAXED),
        __atomic_load_n(&s_frame_generation, __ATOMIC_RELAXED));
}

void video_frame_tracking_reset()
{
    std::lock_guard<std::mutex> lock(g_frame_slots_mutex);
    __atomic_store_n(&s_frame_generation, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_presented_generation, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_backlog_refresh_pending, false, __ATOMIC_RELAXED);
    __atomic_store_n(&s_waiting_slot, -1, __ATOMIC_RELAXED);
    __atomic_store_n(&s_waiting_last_log_us, 0, __ATOMIC_RELAXED);
    for (int i = 0; i < 3; ++i)
    {
        __atomic_store_n(&s_slot_generation[i], 0, __ATOMIC_RELAXED);
        __atomic_store_n(&s_slot_publish_timestamp_us[i], 0, __ATOMIC_RELAXED);
        __atomic_store_n(&s_slot_frame_number[i], 0, __ATOMIC_RELAXED);
    }
}

bool video_frame_mark_presented(uint32_t generation)
{
    uint32_t previous = __atomic_load_n(&s_presented_generation, __ATOMIC_RELAXED);
    while (generation > previous)
    {
        if (__atomic_compare_exchange_n(&s_presented_generation, &previous, generation, true,
                __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        {
            return true;
        }
    }
    return false;
}

void video_stats_record_input_frame(const DECODE_UNIT* decodeUnit)
{
    if (!decodeUnit || decodeUnit->frameNumber <= 0)
    {
        return;
    }

    uint32_t frameNumber = (uint32_t)decodeUnit->frameNumber;
    uint32_t previous    = video_stats::exchange(g_stats.last_frame_number, frameNumber);
    bool backlogRefresh  = __atomic_exchange_n(&s_backlog_refresh_pending, false, __ATOMIC_RELAXED);
    if (previous != 0 && frameNumber > previous + 1)
    {
        video_stats::add(backlogRefresh ? g_stats.frames_dropped_pacer : g_stats.frames_dropped_network,
            frameNumber - previous - 1);
    }
    if (decodeUnit->frameType == FRAME_TYPE_IDR)
    {
        video_stats::increment(g_stats.idr_count);
    }
    else
    {
        video_stats::increment(g_stats.p_slice_count);
    }
    if (decodeUnit->enqueueTimeUs >= decodeUnit->receiveTimeUs)
    {
        video_stats::add(g_stats.reassembly_time_ms,
            (uint32_t)((decodeUnit->enqueueTimeUs - decodeUnit->receiveTimeUs) / 1000ULL));
    }
}

void video_stats_record_corruption(bool invalidLength)
{
    video_stats::increment(g_stats.corruption_frames);
    if (invalidLength)
    {
        video_stats::increment(g_stats.corruption_fullLength);
    }
}

bool video_decode_backlog_requires_refresh(const DECODE_UNIT* decodeUnit)
{
    if (!decodeUnit || decodeUnit->frameType == FRAME_TYPE_IDR)
    {
        return false;
    }

    int pendingFrames = LiGetPendingVideoFrames();
    if (pendingFrames < LOW_LATENCY_BACKLOG_THRESHOLD)
    {
        return false;
    }

    video_stats::increment(g_stats.frames_dropped_pacer);
    __atomic_store_n(&s_backlog_refresh_pending, true, __ATOMIC_RELAXED);
    VITA_DEBUG_LOG("[Video][BACKLOG] %d frames pending; flushing to next IDR", pendingFrames);
    return true;
}

void vitavideo_update_scaling_settings(int width, int height)
{
    // Legacy behavior port: calculate texture size/clipped region
    // and presentation dimensions. Supports the "center_region_only" option.
    image_scaling.texture_width  = SCREEN_WIDTH;
    image_scaling.texture_height = SCREEN_HEIGHT;
    image_scaling.origin_x       = 0;
    image_scaling.origin_y       = 0;
    image_scaling.region_x1      = 0.0f;
    image_scaling.region_y1      = 0.0f;
    image_scaling.region_x2      = (float)image_scaling.texture_width;
    image_scaling.region_y2      = (float)image_scaling.texture_height;

    double scaled_width  = (double)SCREEN_HEIGHT * width / height;
    double scaled_height = (double)SCREEN_WIDTH * height / width;

    if (SCREEN_WIDTH * height == SCREEN_HEIGHT * width)
    {
    }
    else if (SCREEN_WIDTH * height > SCREEN_HEIGHT * width)
    {
        // Taller host (e.g. 4:3) -> scaled_height > SCREEN_HEIGHT
        if (video_runtime::centerRegionOnly())
        {
            image_scaling.texture_height = VITA_DECODER_RESOLUTION((int)vita_round(scaled_height));
            image_scaling.region_y1      = (float)VITA_DECODER_RESOLUTION((int)vita_round((scaled_height - SCREEN_HEIGHT) / 2));
            image_scaling.region_y2      = (float)VITA_DECODER_RESOLUTION((int)vita_round((scaled_height + SCREEN_HEIGHT) / 2));
        }
        else
        {
            image_scaling.texture_width = VITA_DECODER_RESOLUTION((int)vita_round(scaled_width));
            image_scaling.region_x2     = (float)VITA_DECODER_RESOLUTION((int)vita_round(scaled_width));
            image_scaling.origin_x      = (int)vita_round((double)(SCREEN_WIDTH - image_scaling.texture_width) / 2);
        }
    }
    else
    {
        // "Wider" host (e.g. 16:9)
        if (video_runtime::centerRegionOnly())
        {
            image_scaling.texture_width = VITA_DECODER_RESOLUTION((int)vita_round(scaled_width));
            image_scaling.region_x1     = (float)VITA_DECODER_RESOLUTION((int)vita_round((scaled_width - SCREEN_WIDTH) / 2));
            image_scaling.region_x2     = (float)VITA_DECODER_RESOLUTION((int)vita_round((scaled_width + SCREEN_WIDTH) / 2));
        }
        else
        {
            image_scaling.texture_height = VITA_DECODER_RESOLUTION((int)vita_round(scaled_height));
            image_scaling.region_y2      = (float)VITA_DECODER_RESOLUTION((int)vita_round(scaled_height));
            image_scaling.origin_y       = (int)vita_round((double)(SCREEN_HEIGHT - image_scaling.texture_height) / 2);
        }
    }

    int dispW, dispH, offX, offY;
    float screen_aspect = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;
    float video_aspect  = (float)width / (float)height;
    if (video_aspect > screen_aspect)
    {
        dispW = SCREEN_WIDTH;
        dispH = (int)((float)SCREEN_WIDTH / video_aspect);
        offX  = 0;
        offY  = (SCREEN_HEIGHT - dispH) / 2;
    }
    else
    {
        dispH = SCREEN_HEIGHT;
        dispW = (int)((float)SCREEN_HEIGHT * video_aspect);
        offY  = 0;
        offX  = (SCREEN_WIDTH - dispW) / 2;
    }
    image_scaling.display_width  = dispW;
    image_scaling.display_height = dispH;
    image_scaling.offset_x       = offX;
    image_scaling.offset_y       = offY;
    image_scaling.enabled        = true;
}

uint32_t g_decode_min_ms = 999999;
uint32_t g_decode_max_ms = 0;
uint32_t g_decode_sum_ms = 0;
uint32_t g_decode_count  = 0;
