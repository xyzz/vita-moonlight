/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once

#include <Limelight.h>
#include <stdbool.h>
#include <stdint.h>

// Forward declaration para evitar acoplar consumidores del header a vita2d
struct vita2d_texture;

// Macros de resolución para PS Vita (del legacy)
#define ROUND_NEAREST_16(x) (vita_round(((double)(x)) / 16) * 16)
#define VITA_DECODER_RESOLUTION_LOWER_BOUND(x) ((x) < 64 ? 64 : (x))
#define VITA_DECODER_RESOLUTION(x) (VITA_DECODER_RESOLUTION_LOWER_BOUND(ROUND_NEAREST_16(x)))

// Función de redondeo personalizada para PS Vita
static inline double vita_round(double x)
{
    return (x >= 0.0) ? (double)((int)(x + 0.5)) : (double)((int)(x - 0.5));
}

// Estados del sistema de video
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

// Estructura para indicadores visuales
typedef struct
{
    bool activated;
    uint8_t alpha;
    bool plus;
} indicator_status;

// Estructura para escalado de imagen
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

// Callbacks para el sistema de video
extern "C" void vitavideo_start();
extern "C" void vitavideo_stop();
extern "C" void vitavideo_show_poor_net_indicator();
extern "C" void vitavideo_hide_poor_net_indicator();
extern "C" int vitavideo_initialized();

// Estadísticas básicas del pipeline de video (similar a MoonlightSession stats simplificados)
typedef struct
{
    uint32_t frames_decoded; // Frames decodificados y presentados (o listos)
    uint32_t frames_presented; // Frames realmente presentados (puede diferir si se habilita dropping)
    uint32_t frames_dropped_network; // Calculado por gaps en frameNumber
    uint32_t frames_dropped_pacer; // Saltados por frame pacer (need_drop)
    uint32_t idr_count; // Número de NALs IDR
    uint32_t p_slice_count; // Número de NALs de tipo slice no-IDR
    uint32_t current_fps; // FPS medidos última ventana
    uint32_t decoded_fps; // FPS de decodificación medidos última ventana
    uint32_t target_fps; // FPS objetivo (redrawRate negociado)
    uint64_t session_ms; // Tiempo desde primer frame (ms)
    uint32_t reassembly_time_ms; // Acumulado de reassembly (si aplica)
    uint32_t decode_time_ms; // Acumulado decode (estimado si se puede medir)
    uint32_t last_frame_number; // Último frameNumber visto
    uint32_t corruption_frames; // Frames descartados por detectar parámetros inválidos/corrupción
    uint32_t corruption_fullLength; // Conteo de veces que fullLength fue incoherente
} VitaVideoStats;

extern "C" void vitavideo_get_stats(VitaVideoStats* outStats);
extern "C" void vitavideo_reset_stats();

// Modo de presentación ahora siempre inmediato; funciones externas eliminadas

// Funciones de configuración
extern "C" int vitavideo_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags);
extern "C" void vita_cleanup();
extern "C" void vita_full_teardown();
extern "C" int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit);

// Modos legacy eliminados

// Funciones de renderizado (legacy eliminadas; usar VitaVideoRenderer en lugar de vitavideo_draw_streaming*_*)
extern "C" void vitavideo_draw_fps();
extern "C" void vitavideo_draw_indicators();

// Acceso a la textura actual (front) para integración futura con renderer Borealis
extern "C" struct vita2d_texture* vitavideo_get_current_texture();

// Configuración de escalado
void vitavideo_update_scaling_settings(int width, int height);

// Callbacks para Limelight
// Nota: renombrado a _vita_new para evitar colisión con versión legacy en library/moonlight-legacy
extern DECODER_RENDERER_CALLBACKS decoder_callbacks_vita_new;
