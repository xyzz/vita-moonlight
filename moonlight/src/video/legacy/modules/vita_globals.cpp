#include "vita_globals.hpp"

#include <mutex>

#include "gamestream/sps.h"

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
uint32_t frame_publish_timestamp_us = 0; // end-to-end latency tracking

int SCREEN_WIDTH  = 960;
int SCREEN_HEIGHT = 544;

image_scaling_settings image_scaling = { 0 };

bool active_video_thread = false;

// Legacy mode and heuristics
// Legacy modes removed

// Heuristics HEVC abort
bool hevc_abort_flag = false;

// YUV buffers for the non-direct-output path.
uint8_t* decoder_yuv_raw       = NULL;
uint8_t* decoder_yuv_buffer    = NULL;
size_t decoder_yuv_buffer_size = 0;
size_t decoder_yuv_total_alloc = 0;
// RGBA Staging removed
void* decoder_output_phys_ptr   = nullptr;
size_t decoder_output_phys_size = 0;
int decoder_output_phys_block   = -1;
bool decoder_output_phys_mapped = false;
// NVG integration removed (direct render)
bool video_fullscreen_stretch = true; // activated by default to occupy full screen
// low-latency removed: immediate presentation is now the only way
int decoder_output_mode                    = 0; // 0=RGBA direct, 1=YUV420 experimental
bool decoder_tried_direct_texture          = true;
int decoder_src_width                      = 0;
int decoder_src_height                     = 0;
int decoder_width                          = 0;
int decoder_height                         = 0;
bool decoder_use_phys_fallback             = false;
uint8_t* decoder_linear_rgba               = nullptr;
size_t decoder_linear_rgba_size            = 0;
uint8_t* decoder_linear_rgba_guard         = nullptr;
size_t decoder_linear_rgba_guard_size      = 0;
size_t decoder_linear_rgba_total_alloc     = 0;
uint32_t decoder_linear_rgba_pitch_pixels  = 0;
uint32_t decoder_linear_rgba_height        = 0;
int decoder_linear_rgba_memblock           = -1;
bool decoder_linear_rgba_physically_backed = false;

VitaVideoStats g_stats      = { 0 };
uint64_t stats_start_ms     = 0;
uint64_t last_fps_window_ms = 0;
uint32_t curr_fps[2]        = { 0, 0 };
SceUID pacer_thread         = -1;
bool active_pacer_thread    = false;

// Indicadores
indicator_status poor_net_indicator = { 0 };

gs::SpsContext* g_sps_ctx = nullptr; // SPS context active (SPS fix)

// Video status info
VideoStatusInfo g_video_status_info = { VITA_VIDEO_NOT_INIT };

// Video settings snapshot
// Defined elsewhere

// ========================================
// Streaming Configuration Constants
// ========================================

// PS Vita native resolution - ALWAYS used for streaming
// regardless of user settings (settings control host monitor only)
const int VITA_STREAM_WIDTH  = 960;
const int VITA_STREAM_HEIGHT = 544;

// Default streaming parameters
const int VITA_STREAM_DEFAULT_FPS     = 60;
const int VITA_STREAM_DEFAULT_BITRATE = 6000; // 6 Mbps

// Bitrate calculation constants (for auto mode)
const float VITA_STREAM_BITS_PER_PIXEL = 0.2f; // Conservative for H.264
const int VITA_STREAM_MIN_BITRATE      = 5000; // 5 Mbps minimum
const int VITA_STREAM_MAX_BITRATE      = 50000; // 50 Mbps maximum

// ========================================

// Features
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
        if (g_video_settings_snapshot.center_region_only)
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
        if (g_video_settings_snapshot.center_region_only)
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

void vitavideo_configure_screen_resolution(int stream_width)
{
    // Configure screen resolution based on incoming stream width.
    // FFmpeg path does not depend on any "sharpscale" feature, so we pick
    // sensible defaults for common stream widths and fall back to the
    // standard Vita resolution otherwise.
    switch (stream_width)
    {
        case 1920:
            SCREEN_WIDTH  = 1920;
            SCREEN_HEIGHT = 1088; // aligned for decoder
            break;
        case 1280:
            SCREEN_WIDTH  = 1280;
            SCREEN_HEIGHT = 720;
            break;
        default:
            SCREEN_WIDTH  = 960;
            SCREEN_HEIGHT = 544;
            break;
    }
    VITA_DEBUG_LOG("[Video] Configurada resolución de pantalla: %dx%d para stream %d", SCREEN_WIDTH, SCREEN_HEIGHT, stream_width);
}

void yuv_write_canaries()
{
    if (!decoder_yuv_raw || decoder_yuv_total_alloc < 32)
        return;
    decoder_yuv_raw[0]                           = 0xCA;
    decoder_yuv_raw[1]                           = 0xFE;
    decoder_yuv_raw[decoder_yuv_total_alloc - 1] = 0xBE;
    decoder_yuv_raw[decoder_yuv_total_alloc - 2] = 0xEF;
}

bool yuv_check_canaries()
{
    if (!decoder_yuv_raw || decoder_yuv_total_alloc < 32)
        return true;
    return decoder_yuv_raw[0] == 0xCA && decoder_yuv_raw[1] == 0xFE && decoder_yuv_raw[decoder_yuv_total_alloc - 1] == 0xBE && decoder_yuv_raw[decoder_yuv_total_alloc - 2] == 0xEF;
}

uint32_t g_decode_min_ms = 999999;
uint32_t g_decode_max_ms = 0;
uint32_t g_decode_sum_ms = 0;
uint32_t g_decode_count  = 0;

volatile bool g_session_stopping = false;
