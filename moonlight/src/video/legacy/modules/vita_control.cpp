#include <psp2/display.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include "vita_globals.hpp"

// Funciones de control y configuración
extern "C" void vitavideo_show_poor_net_indicator()
{
    poor_net_indicator.activated = true;
}

extern "C" void vitavideo_hide_poor_net_indicator()
{
    memset(&poor_net_indicator, 0, sizeof(indicator_status));
}

extern "C" int vitavideo_initialized()
{
    return video_status != VITA_VIDEO_NOT_INIT;
}

// (Funciones legacy de configuración eliminadas; modos simplificados a pixel_format_mode y flags básicos)

// Funciones de estadísticas
extern "C" void vitavideo_get_stats(VitaVideoStats* outStats)
{
    if (!outStats)
        return;
    *outStats = g_stats;
    if (stats_start_ms)
    {
        outStats->session_ms = vita_monotonic_ms() - stats_start_ms;
    }
}

extern "C" void vitavideo_reset_stats()
{
    memset(&g_stats, 0, sizeof(g_stats));
    stats_start_ms     = vita_monotonic_ms();
    last_fps_window_ms = stats_start_ms;
}

// Exponer textura actual
extern "C" vita2d_texture* vitavideo_get_current_texture()
{
    return FRAME_FRONT();
}
