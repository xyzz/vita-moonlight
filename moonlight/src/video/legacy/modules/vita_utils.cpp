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

// Define constants that may be missing
#ifndef SCE_VIDEODEC_TYPE_HW_AVCDEC
#define SCE_VIDEODEC_TYPE_HW_AVCDEC ((SceVideodecType)0x1001)
#endif

// Monotonic time in ms
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
    // Adjust affinity: allow the pacer to use two cores if available (user 0 and 1)
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1);
    uint64_t last_50_tick      = vita_monotonic_ms();
    uint64_t last_500_tick     = last_50_tick;
    uint64_t last_fps_reset_ms = last_50_tick; // To reset frame_count periodically (like legacy)
    // Synchronize target fps with current settings (curr_fps[1] is updated externally)
    vita_netopt_set_target_fps(curr_fps[1] > 0 ? curr_fps[1] : 60);
    uint32_t fps_window_frames = 0;
    uint32_t logCounter        = 0;
    while (active_pacer_thread)
    {
        sceKernelDelayThread(5 * 1000); // short sleep (~5ms) for granularity without busy-wait
        uint64_t now = vita_monotonic_ms();

        // Granular tick 50ms (loss window / connection)
        if (now - last_50_tick >= 50)
        {
            vita_netopt_tick_50ms();
            last_50_tick = now;
        }
        // General backoff tick (reuse previous function)
        if (now - last_500_tick >= 500)
        {
            vita_netopt_tick();
            last_500_tick = now;
        }

        // Reset frame_count every 1 second (like legacy) to avoid infinite accumulation
        if (now - last_fps_reset_ms >= 1000)
        {
            fps_window_frames = frame_count;
            frame_count       = 0; // Reset frame_count for new 1s window
            if (logCounter % 10 == 0)
            { // Log every 10 seconds (~10x 1000ms)
                VITA_DEBUG_LOG("[Video][PACER][FPS] fps_window=%u need_drop=%d", fps_window_frames, need_drop);
            }
            logCounter++;
            last_fps_reset_ms = now;
        }

        // Consume adaptive drop budget and load into need_drop (legacy variable) for compatibility
        // CHANGE: use fresh fps_window_frames to calculate drops (do not accumulate indefinitely)
        unsigned drops = vita_netopt_consume_drop_budget();
        if (drops)
        {
            // Instead of += (which accumulates), use a model similar to legacy: if fps_window > target, calculate fresh drops
            // For now, add with a reasonable limit to prevent need_drop from exploding
            need_drop += (int)drops;
            if (need_drop > 120)
            { // Limit: do not let it grow beyond 2 seconds of frames at 60fps
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

    // Release buffers that do not affect GXM (to avoid leaks between sessions)
    if (decoder_buffer)
    {
        free(decoder_buffer);
        decoder_buffer      = nullptr;
        decoder_buffer_size = 0;
    }

    // Do not release textures here to avoid GPU crashes if Borealis still uses them.
    // They will be freed in vita_init if the resolution changes.

    // Unlock Pixel Processor
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

    // Destroy decoder instance before terminating library
    if (decoder)
    {
        sceAvcdecDeleteDecoder(decoder);
        free(decoder);
        decoder = nullptr;
    }
    if (decoder_info)
    {
        free(decoder_info);
        decoder_info = nullptr;
    }

    // Terminate the AVC library to allow re-initialization in next session
    if (init)
    {
        sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
    }

    // Mark video subsystem as not initialized and return after waiting for
    // rendering to rest
    video_status = VITA_VIDEO_NOT_INIT;
    VITA_DEBUG_LOG("[Video] soft cleanup completed (buffers freed, AVC termination done, full teardown deferred)");
}

// Stubs (placeholder)
void vitavideo_draw_fps() { }
void vitavideo_draw_indicators() { }

#endif // BOREALIS_USE_GXM
