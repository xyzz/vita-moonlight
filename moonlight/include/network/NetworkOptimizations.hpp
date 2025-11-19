#pragma once
#ifdef __cplusplus
extern "C"
{
#endif

    // Habilita/deshabilita las optimizaciones de IDR
    void vita_netopt_set_enabled(int enable);
    // Fuerza una IDR ignorando debounce
    void vita_netopt_force_idr();
    // Reporta frames perdidos (gap de secuencia)
    void vita_netopt_report_loss(unsigned lostFrames);
    // Tick periódico (cada ~500ms recomendable)
    void vita_netopt_tick();
    // Dump rápido a stdout
    void vita_netopt_dump_stats();
    // Solicitud inteligente (si se invoca manualmente)
    void vita_netopt_request_idr_smart();

    // Exponer estructura de stats (snapshot)
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

    // ===== Extensiones avanzadas (legacy-inspired) =====
    // Frame pacing y frameskip adaptativo
    void vita_netopt_set_target_fps(unsigned fps); // FPS objetivo para cálculo de drops
    void vita_netopt_frame_produced(); // Llamar cuando un frame se decodifica (antes de presentar)
    unsigned vita_netopt_consume_drop_budget(); // Devuelve cuántos frames deberían saltarse (y consume ese presupuesto)

    // Tracking de frames para cálculo de pérdida y estado de conexión
    void vita_netopt_on_frame_seen(unsigned frameIndex); // visto (packet completado o detectado por secuencia)
    void vita_netopt_on_frame_completed(unsigned frameIndex); // frame completo decodificado
    void vita_netopt_on_frame_loss_range(unsigned startFrame, unsigned endFrame); // rango perdido (RFI intento)

    // Tick de alta resolución (llamar cada ~50ms idealmente) para ventana de pérdida
    void vita_netopt_tick_50ms();

    // Estado de conexión derivado
    enum VitaNetConnQuality
    {
        VITA_NET_CONN_OKAY = 0,
        VITA_NET_CONN_WARN = 1,
        VITA_NET_CONN_POOR = 2
    };
    struct VitaNetConnSnapshot
    {
        unsigned intervalMs; // Duración ventana actual
        unsigned goodFrames; // Frames buenos en ventana
        unsigned totalFrames; // Frames esperados (estimado)
        unsigned lossPercent; // (total-good)/total *100
        enum VitaNetConnQuality quality;
    };
    int vita_netopt_get_conn_snapshot(struct VitaNetConnSnapshot* out);

    // RFI (Reference Frame Invalidation) stub: en esta versión solo contabiliza y decide IDR si overflow
    void vita_netopt_try_invalidate_ref_range(unsigned startFrame, unsigned endFrame);

    // Dump extendido (incluye estado de conexión y drops)
    void vita_netopt_dump_extended();

    // ===== Instrumentación de timings de video (latencias) =====
    // Llamar una vez por frame cuando se tiene timing completo. Si no hay presentMs (no low-latency), pasar 0.
    void vita_netopt_on_frame_timing(uint64_t decodeStartMs, uint64_t decodeEndMs, uint64_t presentMs);

    struct VitaNetVideoTimingSnapshot
    {
        // Promedios exponenciales (EMA) en ms
        float avgDecodeMs;
        float avgPresentLatencyMs; // present - arrival/decodeEnd aproximado
        float emaAlpha; // alpha usada (debug)
        uint32_t framesTimed; // cuántos frames alimentaron la estadística
        uint32_t p95DecodeMs; // aproximación p95 (máx de ventana reducida)
        uint32_t p95PresentLatencyMs; // idem
        uint32_t lastDecodeMs;
        uint32_t lastPresentLatencyMs;
        uint32_t forcedIdrRecently; // # IDR forzadas últimos ~10s
        uint32_t waitingIdrMs; // si >0, tiempo acumulado esperando IDR
        uint32_t lossBurstModeActive; // 1 si en modo burst
    };

    int vita_netopt_get_video_timing(struct VitaNetVideoTimingSnapshot* out);

#ifdef __cplusplus
}
#endif
