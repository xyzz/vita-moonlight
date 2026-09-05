#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Enable/disable IDR optimizations
    void vita_netopt_set_enabled(int enable);
    // Force an IDR ignoring debounce
    void vita_netopt_force_idr();
    // Report lost frames (sequence gap)
    void vita_netopt_report_loss(unsigned lostFrames);
    // Periodic tick (every ~500ms recommended)
    void vita_netopt_tick();
    // Fast dump to stdout
    void vita_netopt_dump_stats();
    // Smart Request (if invoked manually)
    void vita_netopt_request_idr_smart();

    // Expose stats structure (snapshot)
    struct VitaNetOptSnapshot
    {
        unsigned idrRequests;
        unsigned suppressedIdr;
        unsigned forcedIdr;
        unsigned lossEvents;
        unsigned framesLostAccum;
        unsigned consecutiveLossBursts;
        unsigned backoffLevel;
        unsigned lastMinIntervalMs;
    };

    int vita_netopt_get_stats(struct VitaNetOptSnapshot* out);

    // Frame tracking to calculate loss and connection status
    void vita_netopt_on_frame_seen(unsigned frameIndex); // seen (packet completed or detected by sequence)
    void vita_netopt_on_frame_completed(unsigned frameIndex); // full frame decoded
    void vita_netopt_on_frame_loss_range(unsigned startFrame, unsigned endFrame); // lost range (RFI attempt)

    // High resolution tick (call every ~50ms ideally) for loss window
    void vita_netopt_tick_50ms();

    // Derived connection status
    enum VitaNetConnQuality
    {
        VITA_NET_CONN_OKAY = 0,
        VITA_NET_CONN_WARN = 1,
        VITA_NET_CONN_POOR = 2
    };
    struct VitaNetConnSnapshot
    {
        unsigned intervalMs; // Current window duration
        unsigned goodFrames; // Good window frames
        unsigned totalFrames; // Expected frames (estimated)
        unsigned lossPercent; // (total-good)/total *100
        enum VitaNetConnQuality quality;
    };
    int vita_netopt_get_conn_snapshot(struct VitaNetConnSnapshot* out);

    // RFI (Reference Frame Invalidation) stub: in this version it only counts and decides IDR if overflow
    void vita_netopt_try_invalidate_ref_range(unsigned startFrame, unsigned endFrame);

    // Extended dump (includes connection status)
    void vita_netopt_dump_extended();

    // ===== Instrumentation of video timings (latencies) =====
    // Call once per frame when you have full timing. If there is no presentMs (no low-latency), pass 0.
    void vita_netopt_on_frame_timing(uint64_t decodeStartMs, uint64_t decodeEndMs, uint64_t presentMs);

    struct VitaNetVideoTimingSnapshot
    {
        // Exponential Averages (EMA) in ms
        float avgDecodeMs;
        float avgPresentLatencyMs; // present - arrival/decodeEnd aproximado
        float emaAlpha; // alpha usada (debug)
        uint32_t framesTimed; // how many frames fed the statistic
        uint32_t p95DecodeMs; // p95 approximation (reduced window max)
        uint32_t p95PresentLatencyMs; // idem
        uint32_t lastDecodeMs;
        uint32_t lastPresentLatencyMs;
        uint32_t forcedIdrRecently; // # IDR forced last ~10s
        uint32_t waitingIdrMs; // if >0, accumulated time waiting for IDR
        uint32_t lossBurstModeActive; // 1 yes in burst mode
    };

    int vita_netopt_get_video_timing(struct VitaNetVideoTimingSnapshot* out);

#ifdef __cplusplus
}
#endif
