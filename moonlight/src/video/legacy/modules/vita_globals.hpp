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
#define LINE_SIZE SCREEN_WIDTH
#define FRAMEBUFFER_SIZE (2 * 1024 * 1024)
#define FRAMEBUFFER_ALIGNMENT (256 * 1024)

// Pure copy failure threshold
#define PURE_COPY_FAILURE_THRESHOLD 3

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
// The renderer reads the latest completed texture. The decoder rotates through
// three write targets so a recently displayed texture is not reused immediately.
extern int frame_display_idx;
extern int frame_write_idx;
#define FRAME_FRONT() (frame_textures[frame_display_idx])
#define FRAME_BACK() (frame_textures[frame_write_idx])
#ifdef __cplusplus
extern std::mutex g_frame_slots_mutex;
#endif

// End-to-end latency: decoder writes timestamp when frame is published,
// renderer reads it when frame is presented to measure pipeline delay
extern uint32_t frame_publish_timestamp_us;

extern image_scaling_settings image_scaling;

extern std::atomic<bool> active_video_thread;

// Legacy modes removed

extern bool hevc_abort_flag;

// Intermediate buffers (only used by experimental YUV mode). In direct RGBA mode they are not used.
extern uint8_t* decoder_yuv_raw;
extern uint8_t* decoder_yuv_buffer;
extern size_t decoder_yuv_buffer_size;
extern size_t decoder_yuv_total_alloc;
// (RGBA and NVG staging removed: we draw directly on the texture)
// Optional RGBA staging buffer for debugging (activated when decoder_output_mode==0 and we want to isolate overflow)
extern uint8_t* decoder_linear_rgba;
extern size_t decoder_linear_rgba_size;
extern uint8_t* decoder_linear_rgba_guard;
extern size_t decoder_linear_rgba_guard_size;
extern size_t decoder_linear_rgba_total_alloc;
extern uint32_t decoder_linear_rgba_pitch_pixels;
extern uint32_t decoder_linear_rgba_height;
extern int decoder_linear_rgba_memblock;
extern bool decoder_linear_rgba_physically_backed;

// Fullscreen presentation mode (stretches without preserving aspect)
extern bool video_fullscreen_stretch;
// low-latency removed: immediate route is standard

// Physical buffer for direct decoder output (when the texture is not accepted)
extern void* decoder_output_phys_ptr;
extern size_t decoder_output_phys_size;
extern int decoder_output_phys_block; // SceUID
extern bool decoder_output_phys_mapped;

// Decoder output strategy:
// 0 = Direct RGBA (preferred, decodes to texture or physical RGBA buffer and copies)
// 1 = YUV420 experimental planar (decodes to YUV buffer and converts CPU to RGBA)
extern int decoder_output_mode;
extern bool decoder_tried_direct_texture;
extern int decoder_src_width;
extern int decoder_src_height;
extern int decoder_width;
extern int decoder_height;
// Indicates if we are using fallback: decoder -> contiguous physical buffer -> memcpy to texture
extern bool decoder_use_phys_fallback;

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
void yuv_write_canaries();
bool yuv_check_canaries();
uint64_t vita_monotonic_ms();
int vitavideo_init_1080p_internal_api(int width, int height, SceVideodecQueryInitInfoHwAvcdec* init);

#endif // VITA_GLOBALS_H
