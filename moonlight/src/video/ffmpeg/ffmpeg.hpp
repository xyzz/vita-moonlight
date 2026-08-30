#pragma once

#include <Limelight.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ffmpeg/modules/ffmpeg_decoder.hpp"

// Forward declarations to avoid including libgamestream directly
struct Data;
struct _APP_LIST;
typedef struct _APP_LIST APP_LIST, *PAPP_LIST;

struct SwsContext;
struct GxmTexture;

typedef struct
{
    GxmTexture* texture;
    int width;
    int height;
    bool has_frame;
    bool direct_memory;
} FFmpegVideoFrame;

typedef struct FFmpegVideoContext
{
    FFmpegDecoderContext decoder;
    FFmpegVideoFrame current_frame;
    void* dr_textures[3];
    int dr_front_idx;
    int dr_back_idx;
    int dr_spare_idx;
    SwsContext* sws_context;
    int sws_src_w;
    int sws_src_h;
    int sws_src_fmt;
    GxmTexture* sw_textures[3];
    int sw_write_idx;
    int sw_last_present_idx;
    GxmTexture* sw_texture;
    int sw_texture_width;
    int sw_texture_height;
    int sw_texture_stride;
    int sw_texture_format;
    int frame_rate;
    int stream_width;
    int stream_height;
    uint64_t last_pts_us;
    uint64_t latency_epoch_offset_us;
    uint64_t last_input_pts_us;
    int last_input_frame_number;
    uint32_t latency_samples;
    uint32_t output_age_floor_us;
    bool using_direct_memory;
    bool is_legacy_mode;
    const char* render_mode;
    bool output_age_floor_valid;
    bool decoder_prime_pending;
    bool decoder_resync_pending;
    bool initialized;
} FFmpegVideoContext;

#ifdef __cplusplus
extern "C"
{
#endif

    int ffmpeg_video_init(FFmpegVideoContext* context, int width, int height, int frame_rate);
    void ffmpeg_video_cleanup(FFmpegVideoContext* context);

    void ffmpeg_video_start(FFmpegVideoContext* context);
    void ffmpeg_video_stop(FFmpegVideoContext* context);

    int ffmpeg_video_decode(FFmpegVideoContext* context, unsigned char* data, int size, int frame_type);
    void ffmpeg_video_render(FFmpegVideoContext* context);

    DECODER_RENDERER_CALLBACKS get_ffmpeg_video_callbacks(void);
    void ffmpeg_process_deferred_releases(void);
    void ffmpeg_video_watchdog_tick(void);

    void ffmpeg_video_set_render_mode(FFmpegVideoContext* context, const char* mode);
    const char* ffmpeg_video_get_render_mode(FFmpegVideoContext* context);

#ifdef __cplusplus
}
#endif
