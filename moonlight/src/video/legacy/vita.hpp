/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once

#include <Limelight.h>
#include <stdbool.h>
#include <stdint.h>

// Forward declaration to avoid coupling header consumers to concrete texture backend
struct GxmTexture;

// Resolution macros for PS Vita (legacy)
#define ROUND_NEAREST_16(x) (vita_round(((double)(x)) / 16) * 16)
#define VITA_DECODER_RESOLUTION_LOWER_BOUND(x) ((x) < 64 ? 64 : (x))
#define VITA_DECODER_RESOLUTION(x) (VITA_DECODER_RESOLUTION_LOWER_BOUND(ROUND_NEAREST_16(x)))

// Custom rounding feature for PS Vita
static inline double vita_round(double x)
{
    return (x >= 0.0) ? (double)((int)(x + 0.5)) : (double)((int)(x - 0.5));
}

// Video system states
enum VideoStatus
{
    VITA_VIDEO_NOT_INIT,
    VITA_VIDEO_INIT_GS,
    VITA_VIDEO_INIT_FRAMEBUFFER,
    VITA_VIDEO_INIT_AVC_LIB,
    VITA_VIDEO_INIT_DECODER_MEMBLOCK,
    VITA_VIDEO_INIT_AVC_DEC,
    VITA_VIDEO_INIT_FRAME_PACER_THREAD,
};

// Structure for visual indicators
typedef struct
{
    bool activated;
    uint8_t alpha;
    bool plus;
} indicator_status;

// Structure for image scaling
typedef struct
{
    int texture_width;
    int texture_height;
    int origin_x;
    int origin_y;
    float region_x1, region_y1, region_x2, region_y2;
    bool enabled;
    int display_width;
    int display_height;
    int offset_x;
    int offset_y;
} image_scaling_settings;

// Callbacks for the video system
extern "C" void vitavideo_start();
extern "C" void vitavideo_stop();
extern "C" void vitavideo_show_poor_net_indicator();
extern "C" void vitavideo_hide_poor_net_indicator();
extern "C" int vitavideo_initialized();

// Basic video pipeline stats (similar to simplified MoonlightSession stats)
typedef struct
{
    uint32_t frames_decoded; // Frames decoded and presented (or ready)
    uint32_t frames_presented; // Frames actually presented (may differ if dropping is enabled)
    uint32_t frames_dropped_network; // Calculated by gaps in frameNumber
    uint32_t frames_dropped_pacer; // Frames dropped to recover latency
    uint32_t idr_count; // Number of IDR NALs
    uint32_t p_slice_count; // Number of non-IDR slice type NALs
    uint32_t current_fps; // FPS measured last window
    uint32_t decoded_fps; // Decoding FPS measured last window
    uint32_t target_fps; // Objective FPS (negotiated redrawRate)
    uint64_t session_ms; // Time since first frame (ms)
    uint32_t reassembly_time_ms; // Accumulated reassembly (if applicable)
    uint32_t decode_time_ms; // Accumulated decode (estimated if it can be measured)
    uint32_t last_frame_number; // Last seen frameNumber
    uint32_t corruption_frames; // Frames discarded due to detecting invalid parameters/corruption
    uint32_t corruption_fullLength; // Count of times fullLength was inconsistent
} VitaVideoStats;

extern "C" void vitavideo_get_stats(VitaVideoStats* outStats);
extern "C" void vitavideo_reset_stats();

// Presentation mode now always immediate; removed external functions

// Configuration functions
extern "C" int vitavideo_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags);
extern "C" void vita_cleanup();
extern "C" void vita_full_teardown();
extern "C" int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit);

// Legacy modes removed

// Rendering functions (legacy removed; use VitaVideoRenderer instead of vitavideo_draw_streaming*_*)
extern "C" void vitavideo_draw_fps();
extern "C" void vitavideo_draw_indicators();

// Access to the current front texture.
extern "C" struct GxmTexture* vitavideo_get_current_texture();

// Scaling settings
void vitavideo_update_scaling_settings(int width, int height);

// Limelight callbacks.
// Note: renamed to _vita_new to avoid collision with legacy version in library/moonlight-legacy
extern DECODER_RENDERER_CALLBACKS decoder_callbacks_vita_new;
