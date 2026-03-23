#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include "vita_globals.hpp"

// External presentation function
// Legacy external presentation removed (migrated to NanoVG pipeline). Keep file for future alternative routes.

// Function to start the video
extern "C" void vitavideo_start()
{
    VITA_DEBUG_LOG("[Video] vitavideo_start called");
    const VideoSettings& settings = g_video_settings_snapshot;
    active_video_thread           = true;
    vita2d_set_vblank_wait(settings.enable_vita_vblank_wait);
    // Note: external presentation enabled by default; not drawn inside submit
    VITA_DEBUG_LOG("[Video] vitavideo_start completado");
    if (stats_start_ms == 0)
    {
        stats_start_ms     = vita_monotonic_ms();
        last_fps_window_ms = stats_start_ms;
    }
}

// Video stop function
extern "C" void vitavideo_stop()
{
    VITA_DEBUG_LOG("[Video] vitavideo_stop called");
    vita2d_set_vblank_wait(true);
    active_video_thread = false;
    VITA_DEBUG_LOG("[Video] vitavideo_stop completado");
}

// Function to configure the pacer
extern "C" void vitavideo_set_pacer(int pacer)
{
    VITA_DEBUG_LOG("[Video] vitavideo_set_pacer: %d", pacer);
    // Implementation of the pacer if necessary
}

// Function to obtain statistics
// Moved to vita_control.cpp

// Feature to get video status
extern "C" int vitavideo_get_status(VideoStatusInfo* status)
{
    if (!status)
        return -1;
    status->status         = video_status;
    status->framesRendered = g_stats.frames_presented;
    status->framesDropped  = g_stats.frames_dropped_pacer + g_stats.frames_dropped_network;
    status->framesDecoded  = g_stats.frames_decoded;
    status->currentFps     = g_stats.current_fps;
    status->targetFps      = g_stats.target_fps;
    status->sessionMs      = stats_start_ms ? vita_monotonic_ms() - stats_start_ms : 0;
    status->decoderWidth   = decoder_src_width;
    status->decoderHeight  = decoder_src_height;
    status->displayWidth   = image_scaling.display_width;
    status->displayHeight  = image_scaling.display_height;
    status->scalingEnabled = image_scaling.enabled;
    return 0;
}
