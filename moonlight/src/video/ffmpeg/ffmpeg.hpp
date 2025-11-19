#pragma once

#include <Limelight.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ffmpeg/modules/ffmpeg_decoder.hpp"

// Forward declarations para evitar incluir libgamestream directamente
struct Data;
struct _APP_LIST;
typedef struct _APP_LIST APP_LIST, *PAPP_LIST;

// Estructuras para el sistema de video FFmpeg
struct SwsContext;
struct vita2d_texture;

typedef struct
{
    vita2d_texture* texture;
    int width;
    int height;
    bool has_frame;
    bool direct_memory;
} FFmpegVideoFrame;

typedef struct FFmpegVideoContext
{
    FFmpegDecoderContext decoder;
    FFmpegVideoFrame current_frame;
    void* dr_textures[2];
    int dr_front_idx;
    int dr_back_idx;
    SwsContext* sws_context;
    vita2d_texture* sw_texture;
    int sw_texture_width;
    int sw_texture_height;
    int sw_texture_stride;
    int frame_rate;
    int stream_width;
    int stream_height;
    uint64_t last_pts_us;
    bool using_direct_memory;
    bool is_legacy_mode;
    const char* render_mode;
    bool initialized;
} FFmpegVideoContext;

// Funciones del sistema de video FFmpeg
#ifdef __cplusplus
extern "C"
{
#endif

    // Inicialización y cleanup
    int ffmpeg_video_init(FFmpegVideoContext* context, int width, int height, int frame_rate);
    void ffmpeg_video_cleanup(FFmpegVideoContext* context);

    // Control del video
    void ffmpeg_video_start(FFmpegVideoContext* context);
    void ffmpeg_video_stop(FFmpegVideoContext* context);

    // Decodificación de frames
    int ffmpeg_video_decode(FFmpegVideoContext* context, unsigned char* data, int size, int frame_type);
    void ffmpeg_video_render(FFmpegVideoContext* context);

    // Callbacks para Limelight
    DECODER_RENDERER_CALLBACKS get_ffmpeg_video_callbacks(void);

    void ffmpeg_video_set_render_mode(FFmpegVideoContext* context, const char* mode);
    const char* ffmpeg_video_get_render_mode(FFmpegVideoContext* context);

#ifdef __cplusplus
}
#endif
