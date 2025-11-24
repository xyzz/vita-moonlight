#include "vita_globals.hpp"
#ifdef BOREALIS_USE_GXM
#include <borealis/extern/nanovg/nanovg.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include <borealis/core/application.hpp>

#include "network/NetworkOptimizations.hpp"
#include "video/pixel_format/pixel_format.hpp"
// External pixel processor
extern PixelFormat::IPixelProcessor* g_pixelProcessor;

// Definir constantes que pueden faltar
#ifndef SCE_VIDEODEC_TYPE_HW_AVCDEC
#define SCE_VIDEODEC_TYPE_HW_AVCDEC ((SceVideodecType)0x1001)
#endif

// Tiempo monotónico en ms
uint64_t vita_monotonic_ms()
{
    return sceKernelGetSystemTimeWide() / 1000ULL;
}

// Thread pacer (limita FPS y setea need_drop)
int vita_pacer_thread_main(SceSize args, void* argp)
{
    (void)args;
    (void)argp;
    VITA_DEBUG_LOG("[Video][PACER] thread iniciado");
    // Ajustar afinidad: permitir que el pacer use dos núcleos si disponibles (user 0 y 1)
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1);
    uint64_t last_50_tick      = vita_monotonic_ms();
    uint64_t last_500_tick     = last_50_tick;
    uint64_t last_fps_reset_ms = last_50_tick; // Para resetear frame_count periódicamente (como legacy)
    // Sincronizar target fps con configuración actual (curr_fps[1] se actualiza externamente)
    vita_netopt_set_target_fps(curr_fps[1] > 0 ? curr_fps[1] : 60);
    uint32_t fps_window_frames = 0;
    uint32_t logCounter        = 0;
    while (active_pacer_thread)
    {
        sceKernelDelayThread(5 * 1000); // sleep corto (~5ms) para granularidad sin busy-wait
        uint64_t now = vita_monotonic_ms();

        // Tick granular 50ms (loss window / conexión)
        if (now - last_50_tick >= 50)
        {
            vita_netopt_tick_50ms();
            last_50_tick = now;
        }
        // Tick backoff general (reusa anterior función)
        if (now - last_500_tick >= 500)
        {
            vita_netopt_tick();
            last_500_tick = now;
        }

        // Resetear frame_count cada 1 segundo (como legacy) para evitar acumulación infinita
        if (now - last_fps_reset_ms >= 1000)
        {
            fps_window_frames = frame_count;
            frame_count       = 0; // Resetear frame_count para nueva ventana de 1s
            if (logCounter % 10 == 0)
            { // Log cada 10 segundos (~10x 1000ms)
                VITA_DEBUG_LOG("[Video][PACER][FPS] fps_window=%u need_drop=%d", fps_window_frames, need_drop);
            }
            logCounter++;
            last_fps_reset_ms = now;
        }

        // Consumir drop budget adaptativo y cargar en need_drop (legacy variable) para compatibilidad
        // CAMBIO: usar fps_window_frames fresco para calcular drops (no acumular indefinidamente)
        unsigned drops = vita_netopt_consume_drop_budget();
        if (drops)
        {
            // En lugar de += (que acumula), usar un modelo similar a legacy: si fps_window > target, calcular drops frescos
            // Por ahora, agregar con un límite razonable para evitar que need_drop explote
            need_drop += (int)drops;
            if (need_drop > 120)
            { // Límite: no dejar que crezca más allá de 2 segundos de frames a 60fps
                need_drop = 120;
            }
        }
    }
    VITA_DEBUG_LOG("[Video][PACER] thread saliendo");
    return 0;
}

extern "C" void vita_cleanup()
{
    VITA_DEBUG_LOG("[Video] vita_cleanup llamado");
    // Signal pacer thread to stop and wait for it to finish to avoid racing
    // with vita2d rendering / GXM driver cleanup. This mirrors the legacy
    // moonlight implementation which waits for the thread end before deleting it.
    if (active_pacer_thread)
    {
        active_pacer_thread = false;
        if (pacer_thread >= 0)
        {
            SceInt32 wait_ret = 0;
            SceUInt timeout   = 10000000; // 10s in microseconds
            // Wait for pacer thread to exit gracefully
            sceKernelWaitThreadEnd(pacer_thread, &wait_ret, &timeout);
            // Delete thread handle
            sceKernelDeleteThread(pacer_thread);
            pacer_thread = -1;
        }
    }
    // Ensure any pending vita2d rendering work is finished before we start
    // tearing down decoder/GXM resources. Calling vita2d_wait_rendering_done()
    // here prevents races where the GPU is still processing frames while we
    // call sceAvcdecDeleteDecoder / sceVideodecTermLibrary.
    if (vita2d_inited)
    {
        VITA_DEBUG_LOG("[Video] Waiting for rendering to finish (post-pacer) before decoder teardown");
        vita2d_wait_rendering_done();
    }

    // Liberar buffers que no afectan GXM (para evitar leaks entre sesiones)
    if (decoder_buffer)
    {
        free(decoder_buffer);
        decoder_buffer      = nullptr;
        decoder_buffer_size = 0;
    }

    // No liberar texturas aquí para evitar crash de GPU si Borealis aún las usa.
    // Se liberarán en vita_init si la resolución cambia.

    // Liberar procesador de píxeles
    if (g_pixelProcessor)
    {
        PixelFormat::destroyProcessor(g_pixelProcessor);
        g_pixelProcessor = nullptr;
    }
    if (decoder_output_phys_mapped && decoder_output_phys_ptr)
    {
        sceGxmUnmapMemory(decoder_output_phys_ptr);
        decoder_output_phys_mapped = false;
    }
    if (decoder_output_phys_block >= 0)
    {
        sceKernelFreeMemBlock(decoder_output_phys_block);
        decoder_output_phys_block = -1;
        decoder_output_phys_ptr   = nullptr;
        decoder_output_phys_size  = 0;
    }

    // Terminate the AVC library to allow re-initialization in next session
    if (init)
    {
        sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
    }

    // Mark video subsystem as not initialized and return after waiting for
    // rendering to quiesce.
    video_status = VITA_VIDEO_NOT_INIT;
    VITA_DEBUG_LOG("[Video] soft cleanup completed (buffers freed, AVC termination done, full teardown deferred)");
}

// Stubs (placeholder)
void vitavideo_draw_fps() { }
void vitavideo_draw_indicators() { }

#endif // BOREALIS_USE_GXM
