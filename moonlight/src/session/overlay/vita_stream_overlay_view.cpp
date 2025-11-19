#include "session/overlay/vita_stream_overlay_view.hpp"

#include <psp2/kernel/threadmgr.h>

#include <borealis.hpp>
#include <cstdio>
#include <cstring>

#include "video/legacy/modules/vita_globals.hpp"
// Evitamos dependencia de std::string por problemas de toolchain Vita
// Renderer legacy eliminado: la presentación ahora la realiza SessionMainView usando VitaVideoRenderer

static uint64_t monotonicMs()
{
    return sceKernelGetSystemTimeWide() / 1000ULL; // micro -> ms
}

VitaStreamOverlayView::VitaStreamOverlayView()
    : BaseOverlay()
{
    setPanelPosition(10.0f, 10.0f);
    setPanelSize(300.0f, 150.0f);
    setPanelAlpha(0.5f); // Semi-transparente
    vitavideo_get_stats(&cached);
}

void VitaStreamOverlayView::onLayout()
{
    // Nada especial: el Box padre controla tamaño; si queremos ocupar todo podemos forzar dimensiones AUTO
    // Podríamos ajustarnos al frame actual si se requiere.
}

void VitaStreamOverlayView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    // El frame de video ya se dibuja en SessionMainView::draw antes de los overlays.

    if (!g_video_settings_snapshot.show_fps)
        return;

    uint64_t now = monotonicMs();
    if (now - lastFetchMs > 500)
    { // refrescar cada 0.5s
        vitavideo_get_stats(&cached);
        lastFetchMs = now;
    }

    // Dibujar el panel base
    BaseOverlay::draw(vg, x, y, width, height, style, ctx);

    // Dibujar las estadísticas encima del panel
    nvgFontSize(vg, 18.0f);
    // Intentar reutilizar fuente de un label estándar si existe; como fallback usar font id 0
    nvgFontFaceId(vg, 0);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));

    char statsBuf[512];
    int off = 0;
    // current_fps ahora refleja presentedFPS en la ventana más reciente calculada por renderer
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "FPS (presented/target): %u/%u\n", (unsigned)cached.current_fps, (unsigned)cached.target_fps);
    // Añadir ratio decoded vs presented
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Frames dec/pres: %u/%u\n", (unsigned)cached.frames_decoded, (unsigned)cached.frames_presented);
    // Mostrar FPS de decodificación calculado por el renderer
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Decoded FPS: %u\n", (unsigned)cached.decoded_fps);
    if (cached.frames_decoded > 0)
    {
        unsigned avgDecodeMs = cached.decode_time_ms / cached.frames_decoded;
        off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Decode(ms avg): %u\n", avgDecodeMs);
    }
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "DroppedNet: %u Pacer: %u\n", (unsigned)cached.frames_dropped_network, (unsigned)cached.frames_dropped_pacer);
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "IDR: %u P: %u\n", (unsigned)cached.idr_count, (unsigned)cached.p_slice_count);
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Session(ms): %u\n", (unsigned)cached.session_ms);
    statsBuf[sizeof(statsBuf) - 1] = '\0';

    const float tx        = panelX + 10.0f;
    const float ty        = panelY + 25.0f;
    const float lineH     = 22.0f;
    float drawY           = ty;
    const char* lineStart = statsBuf;
    const char* p         = statsBuf;
    while (true)
    {
        if (*p == '\n' || *p == '\0')
        {
            if (p - lineStart > 0)
            {
                char lineBuf[128];
                int len = (int)(p - lineStart);
                if (len > 127)
                    len = 127;
                memcpy(lineBuf, lineStart, len);
                lineBuf[len] = '\0';
                nvgText(vg, tx, drawY, lineBuf, 0);
            }
            if (*p == '\0')
                break;
            lineStart = p + 1;
            drawY += lineH;
        }
        ++p;
    }
}
