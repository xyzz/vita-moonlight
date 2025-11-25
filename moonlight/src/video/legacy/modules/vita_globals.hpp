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

// Zero-copy eliminado: se simplifica el pipeline a texturas vita2d
// Se preservan solo métricas mínimas necesarias

// (includes básicos ya movidos arriba)

#include <Limelight.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <vita2d.h>

#include "ConfigManager.hpp"
#include "debug.hpp"
#ifdef __cplusplus
#include "libgamestream/sps.h"
extern gs::SpsContext* g_sps_ctx; // contexto SPS (puntero crudo)
#endif

// --- Mitigación de colisiones de nombres con Borealis ---
// Limelight.h define macros para botones de ratón: BUTTON_LEFT / BUTTON_RIGHT / etc.
// Borealis (borealis/core/input.hpp) declara un enum ControllerButton con miembros
// BUTTON_LEFT, BUTTON_RIGHT, etc. Cuando las macros siguen definidas, el preprocesador
// reemplaza los identificadores por números dentro del enum provocando errores de sintaxis
// (ej: '0x03,' en vez de 'BUTTON_RIGHT,').
// Des-definimos solo los macros conflictivos aquí porque en este módulo (video) no se usan
// directamente los macros de ratón de Limelight; si se necesitan valores se pueden usar
// constantes explícitas (0x01 izquierda, 0x03 derecha) o acceder a Limelight.h directamente
// en otro TU que no incluya este header.
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
#define VITA_DEBUG_LOG(...) vita_debug_log(__VA_ARGS__)

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

// Video status info structure
typedef struct
{
    VideoStatus status;
    uint32_t framesRendered;
    uint32_t framesDropped;
    uint32_t framesDecoded;
    uint32_t currentFps;
    uint32_t targetFps;
    uint64_t sessionMs;
    int decoderWidth;
    int decoderHeight;
    int displayWidth;
    int displayHeight;
    bool scalingEnabled;
} VideoStatusInfo;

// Simple VideoSettings structure
// Note: Using the existing VideoSettings from ConfigManager.hpp

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

extern vita2d_texture* frame_textures[2];
extern int frame_front_idx;
extern int frame_back_idx;
#define FRAME_FRONT() (frame_textures[frame_front_idx])
#define FRAME_BACK() (frame_textures[frame_back_idx])
// Modo de un solo buffer (sin doble buffering). Cuando está activo, FRONT y BACK son el mismo índice.
extern bool single_frame_buffer;
// Flag de inicialización de vita2d (nuevo) para asegurarnos de no llamar APIs sin contexto GXM
extern bool vita2d_inited;

extern image_scaling_settings image_scaling;

extern bool active_video_thread;
extern bool frame_ready_flag;
extern uint32_t frame_count;
extern int need_drop;

// Modos legacy eliminados

extern bool hevc_abort_flag;

// Buffers intermedios (solo usados por modo YUV experimental). En modo RGBA directo no se usan.
extern uint8_t* decoder_yuv_raw;
extern uint8_t* decoder_yuv_buffer;
extern size_t decoder_yuv_buffer_size;
extern size_t decoder_yuv_total_alloc;
// (Staging RGBA y NVG eliminados: dibujamos directo sobre la textura)
// Buffer staging RGBA opcional para depuración (se activa cuando decoder_output_mode==0 y queremos aislar overflow)
extern uint8_t* decoder_linear_rgba;
extern size_t decoder_linear_rgba_size;
extern uint8_t* decoder_linear_rgba_guard;
extern size_t decoder_linear_rgba_guard_size;
extern size_t decoder_linear_rgba_total_alloc;
extern uint32_t decoder_linear_rgba_pitch_pixels;
extern uint32_t decoder_linear_rgba_height;
extern int decoder_linear_rgba_memblock;
extern bool decoder_linear_rgba_physically_backed;

// Modo experimental: replicar comportamiento legacy (un solo buffer + decode->draw inmediato)
extern bool legacy_single_immediate_present; // si true, el submit decodifica y presenta inmediatamente (sin frame_ready_flag)
// Modo de presentación fullscreen (estira sin conservar aspect)
extern bool video_fullscreen_stretch;
// low-latency removido: la ruta inmediata es estándar

// Buffer físico para salida directa del decoder (cuando la textura no es aceptada)
extern void* decoder_output_phys_ptr;
extern size_t decoder_output_phys_size;
extern int decoder_output_phys_block; // SceUID
extern bool decoder_output_phys_mapped;

// Estrategia de salida del decoder:
// 0 = RGBA directo (preferido, decodifica a textura o buffer físico RGBA y copia)
// 1 = YUV420 planar experimental (decodifica a buffer YUV y se convierte en CPU a RGBA)
extern int decoder_output_mode;
extern bool decoder_tried_direct_texture;
extern int decoder_src_width;
extern int decoder_src_height;
extern int decoder_width;
extern int decoder_height;
// Indica si estamos usando fallback: decoder -> buffer físico contiguo -> memcpy a textura
extern bool decoder_use_phys_fallback;

extern VitaVideoStats g_stats;
extern uint64_t stats_start_ms;
extern uint64_t last_fps_window_ms;
extern uint32_t curr_fps[2];
extern SceUID pacer_thread;
extern bool active_pacer_thread;

extern indicator_status poor_net_indicator;

// (SPS context deshabilitado temporalmente en build Vita simplificado)

extern VideoStatusInfo g_video_status_info;

extern ::VideoSettings g_video_settings_snapshot;

// Flag para habilitar/deshabilitar debug logs
extern bool g_debug_log_enabled;

// ========================================
// Streaming Configuration Constants
// ========================================
extern const int VITA_STREAM_WIDTH;
extern const int VITA_STREAM_HEIGHT;
extern const int VITA_STREAM_DEFAULT_FPS;
extern const int VITA_STREAM_DEFAULT_BITRATE;
extern const float VITA_STREAM_BITS_PER_PIXEL;
extern const int VITA_STREAM_MIN_BITRATE;
extern const int VITA_STREAM_MAX_BITRATE;

// Function declarations
void vitavideo_update_scaling_settings(int width, int height);
void vitavideo_configure_screen_resolution(int stream_width);
void yuv_write_canaries();
bool yuv_check_canaries();
uint64_t vita_monotonic_ms();
int vita_pacer_thread_main(SceSize args, void* argp);
int vitavideo_init_1080p_internal_api(int width, int height, SceVideodecQueryInitInfoHwAvcdec* init);

#endif // VITA_GLOBALS_H
