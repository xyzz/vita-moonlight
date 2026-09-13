#include "session/overlay/vita_stream_overlay_view.hpp"

#include <psp2/kernel/threadmgr.h>

#include <borealis.hpp>
#include <cstdio>
#include <cstring>

#include "audio/MicrophoneManager.hpp"
#include "session/connection_telemetry.hpp"
#include "utils/vita_platform.hpp"
#include "video/legacy/modules/vita_globals.hpp"
#include "video/runtime_settings.hpp"

VitaStreamOverlayView::VitaStreamOverlayView()
    : BaseOverlay()
{
    setPanelPosition(10.0f, 10.0f);
    setPanelSize(320.0f, 210.0f);
    setPanelAlpha(0.5f);
    vitavideo_get_stats(&cached);
}

void VitaStreamOverlayView::onLayout()
{
}

void VitaStreamOverlayView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    if (!video_runtime::showFps())
        return;

    uint64_t now = vita::monotonicMilliseconds();
    if (now - lastFetchMs > 500)
    {
        vitavideo_get_stats(&cached);
        lastFetchMs = now;
    }

    BaseOverlay::draw(vg, x, y, width, height, style, ctx);

    nvgFontSize(vg, 18.0f);
    nvgFontFaceId(vg, 0);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));

    char statsBuf[512];
    int off = 0;
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "FPS U/D/T: %u/%u/%u\n",
        (unsigned)cached.current_fps, (unsigned)cached.decoded_fps, (unsigned)cached.target_fps);
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Frames decoded: %u\n", (unsigned)cached.frames_decoded);
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Frames unique/repeat: %u/%u\n",
        (unsigned)cached.frames_presented, (unsigned)cached.frames_repeated);
    if (cached.frames_decoded > 0)
    {
        unsigned avgDecodeMs = cached.decode_time_ms / cached.frames_decoded;
        off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Decode(ms avg): %u\n", avgDecodeMs);
    }
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "DroppedNet: %u Pacer: %u\n", (unsigned)cached.frames_dropped_network, (unsigned)cached.frames_dropped_pacer);
    off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "IDR: %u P: %u\n", (unsigned)cached.idr_count, (unsigned)cached.p_slice_count);

    int mic_rtt = MicrophoneManager::getInstance().getRTT();
    if (mic_rtt >= 0)
    {
        off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Mic Latency: %d ms\n", mic_rtt);
    }
    else if (MicrophoneManager::getInstance().isTransmitting())
    {
        off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Mic Latency: ...\n");
    }

    uint32_t estRtt    = 0;
    uint32_t estRttVar = 0;
    if (connection::getEstimatedRtt(estRtt, estRttVar))
    {
        off += snprintf(statsBuf + off, sizeof(statsBuf) - off, "Ping/RTT: %u ms (var: %u)\n", estRtt, estRttVar);
    }

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
