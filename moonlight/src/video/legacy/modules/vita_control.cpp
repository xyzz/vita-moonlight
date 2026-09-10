#include "vita_globals.hpp"

extern "C" void vitavideo_get_stats(VitaVideoStats* outStats)
{
    if (!outStats)
        return;
    outStats->frames_decoded         = video_stats::load(g_stats.frames_decoded);
    outStats->frames_presented       = video_stats::load(g_stats.frames_presented);
    outStats->frames_dropped_network = video_stats::load(g_stats.frames_dropped_network);
    outStats->frames_dropped_pacer   = video_stats::load(g_stats.frames_dropped_pacer);
    outStats->idr_count              = video_stats::load(g_stats.idr_count);
    outStats->p_slice_count          = video_stats::load(g_stats.p_slice_count);
    outStats->current_fps            = video_stats::load(g_stats.current_fps);
    outStats->decoded_fps            = video_stats::load(g_stats.decoded_fps);
    outStats->target_fps             = video_stats::load(g_stats.target_fps);
    outStats->session_ms             = 0;
    outStats->reassembly_time_ms     = video_stats::load(g_stats.reassembly_time_ms);
    outStats->decode_time_ms         = video_stats::load(g_stats.decode_time_ms);
    outStats->last_frame_number      = video_stats::load(g_stats.last_frame_number);
    outStats->corruption_frames      = video_stats::load(g_stats.corruption_frames);
    outStats->corruption_fullLength  = video_stats::load(g_stats.corruption_fullLength);
}

extern "C" void vitavideo_reset_stats()
{
    memset(&g_stats, 0, sizeof(g_stats));
    stats_start_ms     = 0;
    last_fps_window_ms = 0;
    video_stats::store(g_decode_min_ms, 999999);
    video_stats::store(g_decode_max_ms, 0);
    video_stats::store(g_decode_sum_ms, 0);
    video_stats::store(g_decode_count, 0);
}

extern "C" GxmTexture* vitavideo_get_current_texture()
{
    return FRAME_FRONT();
}
