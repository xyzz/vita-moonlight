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
#define VITA_DECODER_RESOLUTION_LOWER_BOUND(x) ((x) < 64 ? 64 : (x))
#define VITA_DECODER_RESOLUTION(x) (VITA_DECODER_RESOLUTION_LOWER_BOUND((((x) + 15) / 16) * 16))

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
};

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

// Basic video pipeline stats (similar to simplified MoonlightSession stats)
typedef struct
{
    uint32_t frames_decoded;
    uint32_t frames_presented;
    uint32_t frames_repeated;
    uint32_t frames_dropped_network;
    uint32_t frames_dropped_pacer;
    uint32_t idr_count;
    uint32_t p_slice_count;
    uint32_t current_fps;
    uint32_t decoded_fps;
    uint32_t target_fps;
    uint64_t session_ms;
    uint32_t reassembly_time_ms;
    uint32_t decode_time_ms;
    uint32_t last_frame_number;
    uint32_t corruption_frames;
    uint32_t corruption_fullLength;
} VitaVideoStats;

extern "C" void vitavideo_get_stats(VitaVideoStats* outStats);
extern "C" void vitavideo_reset_stats();

// Configuration functions
extern "C" int vitavideo_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags);
extern "C" void vita_cleanup();
extern "C" void vita_full_teardown();
extern "C" int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit);

// Scaling settings
void vitavideo_update_scaling_settings(int width, int height);
