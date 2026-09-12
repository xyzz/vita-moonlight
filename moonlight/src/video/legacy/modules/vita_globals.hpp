#ifndef VITA_GLOBALS_H
#define VITA_GLOBALS_H

#include <Limelight.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include "ConfigManager.hpp"
#include "debug.hpp"
#include "video/gxm_texture.hpp"
#ifdef __cplusplus
#include <atomic>
#include <mutex>

#include "gamestream/sps.h"
extern gs::SpsContext* g_sps_ctx;
#endif

// Limelight mouse macros collide with Borealis controller enum members.
#ifdef BUTTON_LEFT
#undef BUTTON_LEFT
#endif
#ifdef BUTTON_RIGHT
#undef BUTTON_RIGHT
#endif
#ifdef BUTTON_MIDDLE
#undef BUTTON_MIDDLE
#endif
#ifdef BUTTON_X1
#undef BUTTON_X1
#endif
#ifdef BUTTON_X2
#undef BUTTON_X2
#endif

// Local headers
#include "../vita.hpp"

// Defines
#define VITA_DEBUG_LOG(...) vita_log::info(__VA_ARGS__)

#define DECODER_BUFFER_SIZE (128 * 1024)
#define AV_INPUT_BUFFER_PADDING_SIZE 64

#include <stddef.h>
extern int SCREEN_WIDTH;
extern int SCREEN_HEIGHT;
// Extern declarations
extern VideoStatus video_status;
extern uint32_t video_status_canary_pre;
extern uint32_t video_status_canary_post;

extern char* decoder_buffer;
extern size_t decoder_buffer_size;
extern SceAvcdecCtrl* decoder;
extern int decoderblock;
extern size_t decoder_block_size;
extern SceAvcdecQueryDecoderInfo* decoder_info;
extern SceVideodecQueryInitInfoHwAvcdec* init;

extern GxmTexture* frame_textures[3];
extern int frame_display_idx;
extern int frame_write_idx;
#ifdef __cplusplus
extern std::mutex g_frame_slots_mutex;
#endif

struct VideoFrameSnapshot
{
    GxmTexture* texture;
    uint32_t generation;
    uint32_t publish_timestamp_us;
    uint32_t frame_number;
    int slot;
};

bool video_frame_acquire_for_render(VideoFrameSnapshot* snapshot);
void video_frame_publish(int slot, uint32_t frameNumber, uint32_t publishTimestampUs);
void video_frame_wait_for_slot(int slot);
void video_frame_watchdog_tick();
void video_frame_tracking_reset();
bool video_frame_mark_presented(uint32_t generation);
void video_stats_record_input_frame(const DECODE_UNIT* decodeUnit);
void video_stats_record_corruption(bool invalidLength);
bool video_decode_backlog_requires_refresh(const DECODE_UNIT* decodeUnit);

extern image_scaling_settings image_scaling;

extern std::atomic<bool> active_video_thread;

// Fullscreen presentation mode (stretches without preserving aspect)
extern bool video_fullscreen_stretch;
// low-latency removed: immediate route is standard

// Decoder output strategy: 0 = direct RGBA, 1 = YUV420 GPU CSC.
extern int decoder_output_mode;
extern int decoder_width;
extern int decoder_height;

extern VitaVideoStats g_stats;
extern uint64_t stats_start_ms;
extern uint64_t last_fps_window_ms;

namespace video_stats
{

inline uint32_t load(const uint32_t& value)
{
    return __atomic_load_n(&value, __ATOMIC_RELAXED);
}

inline void store(uint32_t& target, uint32_t value)
{
    __atomic_store_n(&target, value, __ATOMIC_RELAXED);
}

inline void increment(uint32_t& target)
{
    __atomic_fetch_add(&target, 1, __ATOMIC_RELAXED);
}

inline void add(uint32_t& target, uint32_t value)
{
    __atomic_fetch_add(&target, value, __ATOMIC_RELAXED);
}

inline uint32_t exchange(uint32_t& target, uint32_t value)
{
    return __atomic_exchange_n(&target, value, __ATOMIC_RELAXED);
}

inline void updateMinimum(uint32_t& target, uint32_t value)
{
    uint32_t current = load(target);
    while (value < current
        && !__atomic_compare_exchange_n(&target, &current, value, true,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED))
    {
    }
}

inline void updateMaximum(uint32_t& target, uint32_t value)
{
    uint32_t current = load(target);
    while (value > current
        && !__atomic_compare_exchange_n(&target, &current, value, true,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED))
    {
    }
}

}

extern uint32_t g_decode_min_ms;
extern uint32_t g_decode_max_ms;
extern uint32_t g_decode_sum_ms;
extern uint32_t g_decode_count;

// Function declarations
void vitavideo_update_scaling_settings(int width, int height);
uint64_t vita_monotonic_ms();
int vitavideo_init_1080p_internal_api(int width, int height, SceVideodecQueryInitInfoHwAvcdec* init);

#endif // VITA_GLOBALS_H
