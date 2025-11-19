// NetworkOptimizations.cpp (wrapper de LiRequestIdrFrame)
#include "network/NetworkOptimizations.hpp"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

extern "C" uint64_t LiGetMillis();
// Función original envuelta por el linker con --wrap
extern "C" void __real_LiRequestIdrFrame(void);

typedef struct
{
    uint32_t idrRequests;
    uint32_t suppressedIdr;
    uint32_t forcedIdr;
    uint32_t lossEvents;
    uint32_t framesLostAccum;
    uint64_t lastRealIdrMs;
    uint64_t lastLossMs;
    uint32_t consecutiveLossBursts;
    uint32_t backoffLevel;
    uint32_t lastMinIntervalMs;
    uint32_t suppressedSinceLastReal;
    uint64_t lastIdrCallBurstStartMs;
    uint32_t idrCallsInBurst;
    // Nueva instrumentación
    uint64_t waitingIdrStartMs; // >0 si estamos en ventana sin frame decodable
    uint32_t forcedIdrWindowCount; // # forzadas en ventana móvil
    uint64_t forcedIdrWindowStart; // inicio ventana (10s)
    uint64_t lastDecodeEndMs; // para detectar gaps
    uint8_t lossBurstMode; // modo agresivo tras burst
    uint64_t lossBurstModeUntil; // timestamp fin modo
} VitaNetOptStats;

static volatile int g_enabled      = 1;
static VitaNetOptStats g_net_stats = { 0 };

static const uint32_t BACKOFF_LEVELS[]    = { 350, 500, 750, 1000, 1500, 2000 };
static const uint32_t BACKOFF_DECAY_MS    = 4000;
static const uint32_t IDR_MAX_SUPPRESS_MS = 2500;
// Timeout duro de espera sin frames decodificados (escalonado)
static const uint32_t IDR_TIMEOUT_1_MS = 400; // primer forzado si ya suprimimos varias
static const uint32_t IDR_TIMEOUT_2_MS = 900; // siempre forzar
// Ventana para contar IDR forzadas recientes
static const uint32_t FORCED_IDR_WINDOW_MS  = 10000;
static const uint32_t LOSS_BURST_THRESHOLD  = 4;
static const uint32_t BURST_FORCE_IDR_COUNT = 3;
// Ventanas y parámetros de conexión
static const uint32_t LOSS_SAMPLE_INTERVAL_MS = 50; // granular (legacy 50ms)
static const uint32_t CONN_WINDOW_MS          = 1000; // ventana de estado
static const uint32_t POOR_LOSS_PCT           = 15; // umbral degradación severa
static const uint32_t WARN_LOSS_PCT           = 5; // umbral aviso

// Frame pacing adaptativo
static unsigned g_target_fps           = 60;
static uint64_t g_pace_window_start    = 0;
static unsigned g_pace_frames_produced = 0; // decodificados en ventana
static int g_drop_budget               = 0; // cuantos frames saltar próximamente

// Tracking de pérdida (visto vs completado)
static unsigned g_interval_good          = 0;
static unsigned g_interval_total         = 0; // estimado (seen)
static uint64_t g_interval_start_ms      = 0;
static unsigned g_loss_percent_cached    = 0;
static uint64_t g_last_loss_tick50       = 0; // último tick de 50ms
static VitaNetConnQuality g_conn_quality = VITA_NET_CONN_OKAY;

// RFI simple
struct RfiRange
{
    unsigned start;
    unsigned end;
};
static RfiRange g_rfi_ranges[8];
static unsigned g_rfi_count     = 0;
static unsigned g_rfi_overflows = 0;

static uint64_t nowMsFallback()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}
static uint64_t monotonicMs()
{
    if (LiGetMillis)
        return LiGetMillis();
    return nowMsFallback();
}

extern "C" void vita_netopt_request_idr_smart()
{
    if (!g_enabled)
    {
        __real_LiRequestIdrFrame();
        return;
    }
    uint64_t now                  = monotonicMs();
    uint64_t since                = now - g_net_stats.lastRealIdrMs;
    uint32_t minInterval          = (g_net_stats.backoffLevel < (sizeof(BACKOFF_LEVELS) / sizeof(BACKOFF_LEVELS[0])) ? BACKOFF_LEVELS[g_net_stats.backoffLevel] : BACKOFF_LEVELS[sizeof(BACKOFF_LEVELS) / sizeof(BACKOFF_LEVELS[0]) - 1]);
    g_net_stats.lastMinIntervalMs = minInterval;

    // Actualizar burst de llamadas: si pasó más de 120ms reiniciar ventana
    if (now - g_net_stats.lastIdrCallBurstStartMs > 120)
    {
        g_net_stats.lastIdrCallBurstStartMs = now;
        g_net_stats.idrCallsInBurst         = 0;
    }
    g_net_stats.idrCallsInBurst++;

    if (g_net_stats.lastRealIdrMs == 0)
    {
        __real_LiRequestIdrFrame();
        g_net_stats.lastRealIdrMs = now;
        g_net_stats.idrRequests++;
        g_net_stats.suppressedSinceLastReal = 0;
        return;
    }
    bool urgent = false;
    // Heurística de urgencia: muchas llamadas en ráfaga sin IDR real reciente
    if (g_net_stats.idrCallsInBurst >= 6 && since > 200)
    {
        urgent = true; // Depacketizer atrapado pidiendo IDR repetidamente
    }
    // Si hemos suprimido demasiadas veces y ya pasaron 500ms, forzar
    if (g_net_stats.suppressedSinceLastReal >= 5 && since > 500)
    {
        urgent = true;
    }
    // Timeout duro escalonado si estamos esperando IDR (no frames buenos)
    if (!urgent && g_net_stats.waitingIdrStartMs)
    {
        uint64_t waitingMs = now - g_net_stats.waitingIdrStartMs;
        if (waitingMs >= IDR_TIMEOUT_2_MS)
            urgent = true;
        else if (waitingMs >= IDR_TIMEOUT_1_MS && g_net_stats.suppressedSinceLastReal >= 2)
            urgent = true;
    }
    // Si la última pérdida fue reciente y el tiempo desde IDR es moderado, permitir antes
    if (!urgent)
    {
        if (since < minInterval)
        {
            g_net_stats.suppressedIdr++;
            g_net_stats.suppressedSinceLastReal++;
            return;
        }
        if (since < IDR_MAX_SUPPRESS_MS && g_net_stats.consecutiveLossBursts == 0)
        {
            g_net_stats.suppressedIdr++;
            g_net_stats.suppressedSinceLastReal++;
            return;
        }
    }
    __real_LiRequestIdrFrame();
    g_net_stats.lastRealIdrMs = now;
    g_net_stats.idrRequests++;
    if (urgent)
    {
        g_net_stats.forcedIdr++; // cuenta como forzado para diagnósticos
        if (g_net_stats.forcedIdrWindowStart == 0 || now - g_net_stats.forcedIdrWindowStart > FORCED_IDR_WINDOW_MS)
        {
            g_net_stats.forcedIdrWindowStart = now;
            g_net_stats.forcedIdrWindowCount = 1;
        }
        else
        {
            g_net_stats.forcedIdrWindowCount++;
        }
    }
    g_net_stats.suppressedSinceLastReal = 0;
}

extern "C" void vita_netopt_force_idr()
{
    uint64_t now = monotonicMs();
    __real_LiRequestIdrFrame();
    g_net_stats.lastRealIdrMs = now;
    g_net_stats.idrRequests++;
    g_net_stats.forcedIdr++;
    if (g_net_stats.forcedIdrWindowStart == 0 || now - g_net_stats.forcedIdrWindowStart > FORCED_IDR_WINDOW_MS)
    {
        g_net_stats.forcedIdrWindowStart = now;
        g_net_stats.forcedIdrWindowCount = 1;
    }
    else
    {
        g_net_stats.forcedIdrWindowCount++;
    }
}

extern "C" void vita_netopt_report_loss(unsigned lostFrames)
{
    if (!g_enabled)
        return;
    g_net_stats.lossEvents++;
    g_net_stats.framesLostAccum += lostFrames;
    g_net_stats.lastLossMs = monotonicMs();
    if (lostFrames >= LOSS_BURST_THRESHOLD)
    {
        g_net_stats.consecutiveLossBursts++;
        if (g_net_stats.backoffLevel < 5)
            g_net_stats.backoffLevel++;
    }
    else if (g_net_stats.consecutiveLossBursts > 0)
    {
        g_net_stats.consecutiveLossBursts--;
    }
    if (g_net_stats.consecutiveLossBursts >= BURST_FORCE_IDR_COUNT)
    {
        vita_netopt_force_idr();
        g_net_stats.consecutiveLossBursts = 0;
    }
}

extern "C" void vita_netopt_tick()
{
    if (!g_enabled)
        return;
    uint64_t now = monotonicMs();
    if (g_net_stats.consecutiveLossBursts && now - g_net_stats.lastLossMs > 1500)
        g_net_stats.consecutiveLossBursts = 0;
    if (g_net_stats.backoffLevel > 0 && now - g_net_stats.lastLossMs > BACKOFF_DECAY_MS)
        g_net_stats.backoffLevel--;
    // Salir de modo burst si expira
    if (g_net_stats.lossBurstMode && now >= g_net_stats.lossBurstModeUntil)
    {
        g_net_stats.lossBurstMode = 0;
    }
    // Reset waitingIdr si tuvimos IDR real hace poco (<120ms) o decode volvió
    if (g_net_stats.waitingIdrStartMs && g_net_stats.lastDecodeEndMs && (now - g_net_stats.lastDecodeEndMs) < 60)
    {
        g_net_stats.waitingIdrStartMs = 0;
    }
}

extern "C" void vita_netopt_set_enabled(int enable) { g_enabled = enable ? 1 : 0; }

extern "C" void vita_netopt_dump_stats()
{
    printf("[NetOpt] IDR real=%u sup=%u forz=%u lossEv=%u lost=%u bursts=%u lvl=%u minInt=%ums\n",
        g_net_stats.idrRequests, g_net_stats.suppressedIdr, g_net_stats.forcedIdr,
        g_net_stats.lossEvents, g_net_stats.framesLostAccum, g_net_stats.consecutiveLossBursts,
        g_net_stats.backoffLevel, g_net_stats.lastMinIntervalMs);
}

extern "C" int vita_netopt_get_stats(struct VitaNetOptSnapshot* out)
{
    if (!out)
        return -1;
    out->idrRequests           = g_net_stats.idrRequests;
    out->suppressedIdr         = g_net_stats.suppressedIdr;
    out->forcedIdr             = g_net_stats.forcedIdr;
    out->lossEvents            = g_net_stats.lossEvents;
    out->framesLostAccum       = g_net_stats.framesLostAccum;
    out->consecutiveLossBursts = g_net_stats.consecutiveLossBursts;
    out->backoffLevel          = g_net_stats.backoffLevel;
    out->lastMinIntervalMs     = g_net_stats.lastMinIntervalMs;
    return 0;
}

// ====== Pacing y frameskip adaptativo ======
extern "C" void vita_netopt_set_target_fps(unsigned fps)
{
    if (fps == 0 || fps > 240)
        return; // sanity
    g_target_fps = fps;
}

extern "C" void vita_netopt_frame_produced()
{
    uint64_t now = monotonicMs();
    if (g_pace_window_start == 0)
        g_pace_window_start = now;
    g_pace_frames_produced++;
    uint64_t elapsed = now - g_pace_window_start;
    if (elapsed >= 1000)
    {
        // Calcular exceso
        if (g_pace_frames_produced > g_target_fps)
        {
            int excess = (int)g_pace_frames_produced - (int)g_target_fps;
            g_drop_budget += excess; // acumular como legacy
            if (g_drop_budget > (int)g_target_fps)
                g_drop_budget = g_target_fps; // cap
        }
        g_pace_frames_produced = 0;
        g_pace_window_start    = now;
    }
}

extern "C" unsigned vita_netopt_consume_drop_budget()
{
    if (g_drop_budget <= 0)
        return 0;
    unsigned drops = (unsigned)g_drop_budget;
    g_drop_budget  = 0;
    return drops;
}

// ====== Tracking de frames y pérdida ======
extern "C" void vita_netopt_on_frame_seen(unsigned frameIndex)
{
    (void)frameIndex;
    uint64_t now = monotonicMs();
    if (g_interval_start_ms == 0)
        g_interval_start_ms = now;
    g_interval_total++;
}

extern "C" void vita_netopt_on_frame_completed(unsigned frameIndex)
{
    (void)frameIndex;
    g_interval_good++;
    // Frame completado -> ya no esperamos IDR si había espera
    g_net_stats.waitingIdrStartMs = 0;
}

static void recompute_conn_quality()
{
    if (g_interval_total == 0)
    {
        g_loss_percent_cached = 0;
        g_conn_quality        = VITA_NET_CONN_OKAY;
        return;
    }
    unsigned lost         = g_interval_total > g_interval_good ? (g_interval_total - g_interval_good) : 0;
    g_loss_percent_cached = (lost * 100) / g_interval_total;
    if (g_loss_percent_cached >= POOR_LOSS_PCT)
        g_conn_quality = VITA_NET_CONN_POOR;
    else if (g_loss_percent_cached >= WARN_LOSS_PCT)
        g_conn_quality = VITA_NET_CONN_WARN;
    else
        g_conn_quality = VITA_NET_CONN_OKAY;
}

extern "C" void vita_netopt_tick_50ms()
{
    uint64_t now = monotonicMs();
    if (g_last_loss_tick50 == 0)
        g_last_loss_tick50 = now;
    if (now - g_last_loss_tick50 >= LOSS_SAMPLE_INTERVAL_MS)
    {
        g_last_loss_tick50 = now;
        // No reiniciamos cada 50ms, solo dejamos correr ventana de 1s
        if (g_interval_start_ms && now - g_interval_start_ms >= CONN_WINDOW_MS)
        {
            // Recalcular calidad sobre la ventana
            recompute_conn_quality();
            // Reiniciar ventana
            g_interval_start_ms = now;
            g_interval_good     = 0;
            g_interval_total    = 0;
        }
        else
        {
            recompute_conn_quality();
        }
    }
}

extern "C" int vita_netopt_get_conn_snapshot(struct VitaNetConnSnapshot* out)
{
    if (!out)
        return -1;
    uint64_t now = monotonicMs();
    if (g_interval_start_ms == 0)
    {
        out->intervalMs  = 0;
        out->goodFrames  = 0;
        out->totalFrames = 0;
        out->lossPercent = 0;
        out->quality     = VITA_NET_CONN_OKAY;
        return 0;
    }
    out->intervalMs  = (unsigned)(now - g_interval_start_ms);
    out->goodFrames  = g_interval_good;
    out->totalFrames = g_interval_total;
    out->lossPercent = g_loss_percent_cached;
    out->quality     = g_conn_quality;
    return 0;
}

// ====== RFI rango simple ======
extern "C" void vita_netopt_try_invalidate_ref_range(unsigned startFrame, unsigned endFrame)
{
    if (startFrame > endFrame)
        return;
    if (g_rfi_count < (sizeof(g_rfi_ranges) / sizeof(g_rfi_ranges[0])))
    {
        g_rfi_ranges[g_rfi_count].start = startFrame;
        g_rfi_ranges[g_rfi_count].end   = endFrame;
        g_rfi_count++;
    }
    else
    {
        g_rfi_overflows++;
        // Overflow -> forzar IDR (similar a legacy queueFrameInvalidationTuple)
        vita_netopt_force_idr();
        g_rfi_count = 0; // reset lista
    }
}

extern "C" void vita_netopt_on_frame_loss_range(unsigned startFrame, unsigned endFrame)
{
    vita_netopt_report_loss(endFrame >= startFrame ? (endFrame - startFrame + 1) : 1);
    vita_netopt_try_invalidate_ref_range(startFrame, endFrame);
    // Activar modo burst agresivo (reduce supresión) durante 500ms
    uint64_t now                   = monotonicMs();
    g_net_stats.lossBurstMode      = 1;
    g_net_stats.lossBurstModeUntil = now + 500;
    // Si todavía no había waitingIdrStart, inícialo (pérdida grave probablemente impide decode)
    if (!g_net_stats.waitingIdrStartMs)
        g_net_stats.waitingIdrStartMs = now;
}

extern "C" void vita_netopt_dump_extended()
{
    VitaNetOptSnapshot s;
    vita_netopt_get_stats(&s);
    VitaNetConnSnapshot c;
    vita_netopt_get_conn_snapshot(&c);
    printf("[NetOptX] IDR(real=%u sup=%u f=%u winF=%u) lossEv=%u lost=%u bursts=%u lvl=%u min=%u waiting=%s | Conn t=%ums g=%u tot=%u lp=%u%% q=%d drop=%d RFIc=%u RFIof=%u burstMode=%u\n",
        s.idrRequests, s.suppressedIdr, s.forcedIdr, g_net_stats.forcedIdrWindowCount, s.lossEvents, s.framesLostAccum, s.consecutiveLossBursts,
        s.backoffLevel, s.lastMinIntervalMs,
        (g_net_stats.waitingIdrStartMs ? "YES" : "no"),
        c.intervalMs, c.goodFrames, c.totalFrames, c.lossPercent, (int)c.quality, g_drop_budget, g_rfi_count, g_rfi_overflows, (unsigned)g_net_stats.lossBurstMode);
}

// Intercepción global del símbolo LiRequestIdrFrame
extern "C" void __wrap_LiRequestIdrFrame(void)
{
    vita_netopt_request_idr_smart();
}

// ====== Timing de video ======
typedef struct
{
    float avgDecodeMs;
    float avgPresentLatencyMs;
    float emaAlpha;
    uint32_t framesTimed;
    uint32_t p95DecodeMs;
    uint32_t p95PresentMs;
    uint32_t lastDecodeMs;
    uint32_t lastPresentMs;
    uint32_t ringDecode[32];
    uint32_t ringPresent[32];
    uint8_t ringPos;
} VitaNetVideoTiming;

static VitaNetVideoTiming g_video_timing = { 0 };
static const float TIMING_EMA_ALPHA      = 0.12f; // respuesta rápida sin demasiado ruido

static uint32_t compute_p95(uint32_t* arr, uint32_t count)
{
    if (count == 0)
        return 0;
    // copia pequeña y selección parcial (n<=32)
    uint32_t tmp[32];
    if (count > 32)
        count = 32;
    for (uint32_t i = 0; i < count; i++)
        tmp[i] = arr[i];
    // ordenar simple (insertion) porque n pequeño
    for (uint32_t i = 1; i < count; i++)
    {
        uint32_t v = tmp[i];
        uint32_t j = i;
        while (j > 0 && tmp[j - 1] > v)
        {
            tmp[j] = tmp[j - 1];
            j--;
        }
        tmp[j] = v;
    }
    uint32_t idx = (uint32_t)((count - 1) * 0.95f);
    return tmp[idx];
}

extern "C" void vita_netopt_on_frame_timing(uint64_t decodeStartMs, uint64_t decodeEndMs, uint64_t presentMs)
{
    if (!decodeStartMs || !decodeEndMs)
        return;
    uint32_t decodeMs         = (uint32_t)(decodeEndMs - decodeStartMs);
    uint32_t presentLatencyMs = 0;
    if (presentMs && presentMs >= decodeEndMs)
        presentLatencyMs = (uint32_t)(presentMs - decodeEndMs);

    VitaNetVideoTiming* vt = &g_video_timing;
    if (vt->framesTimed == 0)
    {
        vt->avgDecodeMs         = (float)decodeMs;
        vt->avgPresentLatencyMs = (float)presentLatencyMs;
    }
    else
    {
        vt->avgDecodeMs         = (1.0f - TIMING_EMA_ALPHA) * vt->avgDecodeMs + TIMING_EMA_ALPHA * (float)decodeMs;
        vt->avgPresentLatencyMs = (1.0f - TIMING_EMA_ALPHA) * vt->avgPresentLatencyMs + TIMING_EMA_ALPHA * (float)presentLatencyMs;
    }
    vt->lastDecodeMs     = decodeMs;
    vt->lastPresentMs    = presentLatencyMs;
    vt->emaAlpha         = TIMING_EMA_ALPHA;
    uint8_t pos          = vt->ringPos & 31;
    vt->ringDecode[pos]  = decodeMs;
    vt->ringPresent[pos] = presentLatencyMs;
    vt->ringPos++;
    if (vt->framesTimed < 0xFFFFFFFF)
        vt->framesTimed++;
    uint32_t used    = vt->framesTimed < 32 ? vt->framesTimed : 32;
    vt->p95DecodeMs  = compute_p95(vt->ringDecode, used);
    vt->p95PresentMs = compute_p95(vt->ringPresent, used);

    // Señalar último decode para lógica de waiting
    g_net_stats.lastDecodeEndMs = decodeEndMs;
}

extern "C" int vita_netopt_get_video_timing(struct VitaNetVideoTimingSnapshot* out)
{
    if (!out)
        return -1;
    out->avgDecodeMs          = g_video_timing.avgDecodeMs;
    out->avgPresentLatencyMs  = g_video_timing.avgPresentLatencyMs;
    out->emaAlpha             = g_video_timing.emaAlpha;
    out->framesTimed          = g_video_timing.framesTimed;
    out->p95DecodeMs          = g_video_timing.p95DecodeMs;
    out->p95PresentLatencyMs  = g_video_timing.p95PresentMs;
    out->lastDecodeMs         = g_video_timing.lastDecodeMs;
    out->lastPresentLatencyMs = g_video_timing.lastPresentMs;
    out->forcedIdrRecently    = g_net_stats.forcedIdrWindowCount;
    out->waitingIdrMs         = g_net_stats.waitingIdrStartMs ? (uint32_t)(monotonicMs() - g_net_stats.waitingIdrStartMs) : 0;
    out->lossBurstModeActive  = g_net_stats.lossBurstMode ? 1u : 0u;
    return 0;
}
