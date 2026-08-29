#include "vita_globals.hpp"
#ifdef BOREALIS_USE_GXM
#include <borealis/extern/nanovg/nanovg.h>
#include <borealis/extern/nanovg/nanovg_gxm_utils.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <borealis/core/application.hpp>

#include "network/NetworkOptimizations.hpp"
#include "video/pixel_format/pixel_format.hpp"
// External pixel processor
extern PixelFormat::IPixelProcessor* g_pixelProcessor;

// Define constants that may be missing
#ifndef SCE_VIDEODEC_TYPE_HW_AVCDEC
#define SCE_VIDEODEC_TYPE_HW_AVCDEC ((SceVideodecType)0x1001)
#endif

static inline void wait_for_borealis_gxm_idle()
{
    // Avoid direct sceGxmFinish calls here: they have shown instability on some builds.
    sceKernelDelayThread(1000);
}

// Monotonic time in ms
uint64_t vita_monotonic_ms()
{
    return sceKernelGetSystemTimeWide() / 1000ULL;
}

// Periodic network maintenance for the legacy decoder.
int vita_pacer_thread_main(SceSize args, void* argp)
{
    (void)args;
    (void)argp;
    VITA_DEBUG_LOG("[Video][PACER] thread iniciado");
    uint64_t last_50_tick  = vita_monotonic_ms();
    uint64_t last_500_tick = last_50_tick;
    while (active_pacer_thread)
    {
        sceKernelDelayThread(50 * 1000);
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
    VITA_DEBUG_LOG("[Video] Waiting for rendering to finish (post-pacer) before decoder teardown");
    wait_for_borealis_gxm_idle();

    // Release buffers that do not affect GXM (to avoid leaks between sessions)
    if (decoder_buffer)
    {
        free(decoder_buffer);
        decoder_buffer      = nullptr;
        decoder_buffer_size = 0;
    }

    // Do not release textures here to avoid GPU crashes if Borealis still uses them.
    // They will be freed in vita_init if the resolution changes.

    // Pixel processor objects are not used in the decode hot path.
    // Keep pointer null here to avoid virtual cleanup on potentially stale pointers.
    g_pixelProcessor = nullptr;
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

void vitavideo_draw_fps() { }
void vitavideo_draw_indicators() { }

#endif // BOREALIS_USE_GXM
