#include "vita_globals.hpp"
#ifdef BOREALIS_USE_GXM
#include <borealis/extern/nanovg/nanovg.h>
#include <borealis/extern/nanovg/nanovg_gxm_utils.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/videodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <borealis/core/application.hpp>

#include "utils/vita_platform.hpp"
#include "video/pixel_format/pixel_format.hpp"
// External pixel processor
extern PixelFormat::IPixelProcessor* g_pixelProcessor;

// Define constants that may be missing
#ifndef SCE_VIDEODEC_TYPE_HW_AVCDEC
#define SCE_VIDEODEC_TYPE_HW_AVCDEC ((SceVideodecType)0x1001)
#endif

uint64_t vita_monotonic_ms()
{
    return vita::monotonicMilliseconds();
}

extern "C" void vita_cleanup()
{
    VITA_DEBUG_LOG("[Video] vita_cleanup llamado");
    // Ensure any pending vita2d rendering work is finished before we start
    // tearing down decoder/GXM resources. Calling vita2d_wait_rendering_done()
    // here prevents races where the GPU is still processing frames while we
    // call sceAvcdecDeleteDecoder / sceVideodecTermLibrary.
    VITA_DEBUG_LOG("[Video] Waiting for rendering to finish before decoder teardown");
    vita::delayBeforeGxmTeardown();

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

#endif // BOREALIS_USE_GXM
