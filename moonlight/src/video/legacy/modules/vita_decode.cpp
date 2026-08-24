#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/videodec.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "vita_globals.hpp"
#ifdef __cplusplus
#include "gamestream/sps.h"
extern gs::SpsContext* g_sps_ctx;
#endif
#include <borealis/extern/nanovg/nanovg.h>

#include <borealis/core/application.hpp>

#include "Limelight.h"
#include "debug.hpp"
#include "network/NetworkOptimizations.hpp"
#include "session/vita_session.hpp"
#include "video/pixel_format/pixel_format.hpp"
#include "video/render_mode_cache.hpp"
#include "vita_sceAvcInternal.hpp"

// Global pixel format processor (modular RGBA/YUV handling)
PixelFormat::IPixelProcessor* g_pixelProcessor = nullptr;

typedef struct ScePafInit
{
    SceSize global_heap_size;
    int a2;
    int a3;
    int cdlg_mode;
    int heap_opt_param1;
    int heap_opt_param2;
} ScePafInit;

int vitavideo_init_1080p_internal_api(int width, int height, SceVideodecQueryInitInfoHwAvcdec* init)
{
    static bool modules_loaded = false;

    if (!modules_loaded)
    {
        SceSysmoduleOpt sysmodule_opt;
        ScePafInit init_param;
        memset(&init_param, 0, sizeof(init_param));
        init_param.global_heap_size = 4 * 1024 * 1024;
        init_param.a2               = 0x0000EA60;
        init_param.a3               = 0x00040000;
        init_param.cdlg_mode        = 0;
        init_param.heap_opt_param1  = 0;
        init_param.heap_opt_param2  = 0;

        memset(&sysmodule_opt, 0, sizeof(sysmodule_opt));
        sysmodule_opt.flags = 0;

        int ret_paf = sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(init_param), &init_param, &sysmodule_opt);
        VITA_DEBUG_LOG("[Video][1080p] sceSysmoduleLoadModuleInternalWithArg(PAF): 0x%x", ret_paf);

        int ret_ini = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_INI_FILE_PROCESSOR);
        VITA_DEBUG_LOG("[Video][1080p] sceSysmoduleLoadModuleInternal(INI_FILE_PROCESSOR): 0x%x", ret_ini);

        int ret_avc = sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC);
        VITA_DEBUG_LOG("[Video][1080p] sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC): 0x%x", ret_avc);

        int ret_config = sceVideodecSetConfigInternal(SCE_VIDEODEC_TYPE_HW_AVCDEC, 2);
        VITA_DEBUG_LOG("[Video][1080p] sceVideodecSetConfigInternal: 0x%x", ret_config);

        int ret_mode = sceAvcdecSetDecodeModeInternal(SCE_VIDEODEC_TYPE_HW_AVCDEC, SCE_AVCDEC_MODE_EXTENDED);
        VITA_DEBUG_LOG("[Video][1080p] sceAvcdecSetDecodeModeInternal: 0x%x", ret_mode);

        modules_loaded = true;
    }

    int ret = sceVideodecInitLibraryInternal(SCE_VIDEODEC_TYPE_HW_AVCDEC, init);
    if (ret == 0x80620808)
    {
        VITA_DEBUG_LOG("[Video][1080p] sceVideodecInitLibraryInternal already initialized");
        return 0;
    }
    else if (ret < 0)
    {
        VITA_DEBUG_LOG("[Video][1080p] Error sceVideodecInitLibraryInternal: 0x%x", ret);
        return ret;
    }

    VITA_DEBUG_LOG("[Video][1080p] sceVideodecInitLibraryInternal success");
    return 0;
}

#ifndef SCE_AVCDEC_PIXELFORMAT_YUV420_PLANAR
#define SCE_AVCDEC_PIXELFORMAT_YUV420_PLANAR 0x4
#endif

static inline uint64_t monotonicMs_local()
{
    if (LiGetMillis)
        return LiGetMillis();
    return 0;
}

// Internal control to avoid log spam in physical fallback
static bool decoder_phys_fallback_permanent_disable = false; // if true do not retry physical alloc
static int decoder_phys_fallback_fail_count         = 0;

extern "C" int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit)
{
    static uint32_t vd_submit_counter            = 0;
    static bool last_call_had_output             = false;
    static const uint64_t texture_guard_sig_tail = 0xA55AA55AA55AA55AULL;
    static const uint64_t texture_guard_sig_head = 0xDEADBEEFCAFEBABEULL;
    static const uint8_t texture_guard_fill      = 0xD6;
    // Destino actual (single buffer) => FRAME_BACK() == FRAME_FRONT() mientras single_frame_buffer=true
    uint8_t* texBack         = (uint8_t*)gxm_texture_get_datap(FRAME_BACK());
    unsigned int strideBytes = gxm_texture_get_stride(FRAME_BACK());
    if (!strideBytes)
        strideBytes = image_scaling.texture_width * 4;
    // Force no physical fallback in RGBA mode (stable debugging)
    if (vd_submit_counter < 4 || (vd_submit_counter % 300) == 0)
    {
        VITA_DEBUG_LOG("[Video] submit_decode_unit #%u", vd_submit_counter);
    }

    SceAvcdecAu au                      = { 0 };
    SceAvcdecArrayPicture array_picture = { 0 };
    SceAvcdecPicture picture            = { 0 };
    SceAvcdecPicture* pictures          = &picture;
    array_picture.numOfElm              = 1;
    array_picture.pPicture              = &pictures;

    picture.size      = sizeof(picture);
    uint32_t alignedW = (decoder_width > 0 ? decoder_width : image_scaling.texture_width);
    uint32_t baseH    = (decoder_height > 0 ? decoder_height : image_scaling.texture_height);
    bool decodeYuv    = (decoder_output_mode == 1);

    // Staging logic is handled by the modular pixel processor

    bool useFallbackBuffer       = false;
    uint8_t* fallbackPtr         = nullptr;
    uint32_t fallbackPitchPixels = 0;
    if (!decodeYuv && decoder_use_phys_fallback)
    {
        size_t needed               = (size_t)image_scaling.texture_width * (size_t)image_scaling.texture_height * 4;
        constexpr size_t kPhysAlign = 256 * 1024;
        size_t allocSize            = (needed + (kPhysAlign - 1)) & ~(kPhysAlign - 1);
        if (allocSize < needed)
        {
            allocSize = needed;
        }
        decoder_output_phys_size = allocSize;
        if (!decoder_output_phys_ptr && decoder_output_phys_size > 0)
        {
            int types[] = {
                SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
                SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW
            };
            for (unsigned ti = 0; ti < sizeof(types) / sizeof(types[0]) && !decoder_output_phys_ptr; ++ti)
            {
                decoder_output_phys_block = sceKernelAllocMemBlock("dec_phys_rgba", types[ti], decoder_output_phys_size, NULL);
                if (decoder_output_phys_block < 0)
                {
                    VITA_DEBUG_LOG("[Video][FB_PHY] Alloc phys type=0x%X err=0x%08X", types[ti], decoder_output_phys_block);
                    continue;
                }
                if (sceKernelGetMemBlockBase(decoder_output_phys_block, &decoder_output_phys_ptr) < 0 || !decoder_output_phys_ptr)
                {
                    VITA_DEBUG_LOG("[Video][FB_PHY] GetMemBlockBase fallo type=0x%X", types[ti]);
                    sceKernelFreeMemBlock(decoder_output_phys_block);
                    decoder_output_phys_block = -1;
                }
                else
                {
                    if (!decoder_output_phys_mapped)
                    {
                        int mapRes = sceGxmMapMemory(decoder_output_phys_ptr, (SceSize)decoder_output_phys_size, SCE_GXM_MEMORY_ATTRIB_RW);
                        if (mapRes < 0)
                        {
                            VITA_DEBUG_LOG("[Video][FB_PHY] Map memoria fallo type=0x%X err=0x%08X", types[ti], mapRes);
                            sceKernelFreeMemBlock(decoder_output_phys_block);
                            decoder_output_phys_block = -1;
                            decoder_output_phys_ptr   = nullptr;
                            decoder_output_phys_size  = 0;
                            continue;
                        }
                        decoder_output_phys_mapped = true;
                    }
                    decoder_phys_fallback_fail_count = 0;
                    VITA_DEBUG_LOG("[Video][FB_PHY] Buffer físico RGBA asignado size=%lu type=0x%X", (unsigned long)decoder_output_phys_size, types[ti]);
                }
            }
        }
        if (decoder_output_phys_ptr)
        {
            fallbackPtr         = static_cast<uint8_t*>(decoder_output_phys_ptr);
            fallbackPitchPixels = image_scaling.texture_width;
            useFallbackBuffer   = true;
        }
        else if (!decoder_phys_fallback_permanent_disable)
        {
            decoder_phys_fallback_fail_count++;
            if (decoder_phys_fallback_fail_count > 2)
            {
                decoder_phys_fallback_permanent_disable = true;
                decoder_use_phys_fallback               = false;
                VITA_DEBUG_LOG("[Video][FB_PHY] Fallback físico RGBA deshabilitado tras %d fallos", decoder_phys_fallback_fail_count);
            }
        }
    }

    // Configure decoder output directly from mode to avoid virtual dispatch in decode thread.
    bool decodeUsesFallback = false;
    uint8_t* decodeTarget   = nullptr;
    picture.frame.pixelType = decodeYuv ? SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER : SCE_AVCDEC_PIXELFORMAT_RGBA8888;
    decodeTarget            = texBack;

    if (!decodeTarget)
    {
        VITA_DEBUG_LOG("[Video][ERR] No hay destino válido para decodificar (modo=%d)", decoder_output_mode);
        return DR_NEED_IDR;
    }

    picture.frame.frameWidth       = alignedW;
    picture.frame.horizontalSize   = alignedW;
    uint32_t frameHeightForDecoder = baseH;
    if (decodeUsesFallback)
    {
        frameHeightForDecoder = baseH;
    }

    picture.frame.frameHeight  = frameHeightForDecoder;
    picture.frame.verticalSize = frameHeightForDecoder;

    uint32_t framePitchPixels = 0;
    if (decodeYuv)
    {
        // YUV raster decode path uses aligned width in pixels as pitch.
        framePitchPixels = alignedW;
    }
    else if (decodeUsesFallback)
    {
        framePitchPixels = fallbackPitchPixels ? fallbackPitchPixels : alignedW;
    }
    else
    {
        framePitchPixels = strideBytes / 4;
        if (framePitchPixels == 0)
        {
            framePitchPixels = alignedW;
        }
    }
    picture.frame.framePitch = framePitchPixels;

    picture.frame.pixelType = decodeYuv ? SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER : SCE_AVCDEC_PIXELFORMAT_RGBA8888;

    const uint8_t* cpuPushPtr  = nullptr;
    uint32_t cpuPushPitchBytes = 0;

    picture.frame.pPicture[0] = decodeTarget; // decode directly to the chosen buffer
    if (decodeYuv)
    {
        picture.frame.pPicture[1] = decodeTarget + (alignedW * baseH);
    }
    else
    {
        picture.frame.pPicture[1] = NULL;
    }

    if (decoder_buffer_size < (decodeUnit->fullLength + AV_INPUT_BUFFER_PADDING_SIZE))
    {
        decoder_buffer      = (char*)realloc(decoder_buffer, decodeUnit->fullLength + AV_INPUT_BUFFER_PADDING_SIZE);
        decoder_buffer_size = decodeUnit->fullLength + AV_INPUT_BUFFER_PADDING_SIZE;
        if (!decoder_buffer)
            return DR_NEED_IDR;
    }
    PLENTRY entry   = decodeUnit->bufferList;
    uint32_t length = 0;
    while (entry)
    {
        if (entry->bufferType == BUFFER_TYPE_SPS)
        {
#ifdef __cplusplus
            if (g_sps_ctx)
            {
                uint32_t before = length;
                g_sps_ctx->fix(entry, GS_SPS_BITSTREAM_FIXUP, (uint8_t*)decoder_buffer, &length);
                VITA_DEBUG_LOG("[Video][SPS] Fix aplicado (in=%u out=%u)", entry->length, (unsigned)(length - before));
            }
            else
#endif
            {
                memcpy(decoder_buffer + length, entry->data, entry->length);
                length += entry->length;
            }
        }
        else
        {
            memcpy(decoder_buffer + length, entry->data, entry->length);
            length += entry->length;
        }
        entry = entry->next;
    }
    au.es.pBuf   = decoder_buffer;
    au.es.size   = decodeUnit->fullLength;
    au.dts.lower = au.dts.upper = au.pts.lower = au.pts.upper = 0xFFFFFFFF;

    if (vd_submit_counter < 2)
    {
        VITA_DEBUG_LOG("[Video] AU size=%u fullLength=%u", au.es.size, decodeUnit->fullLength);
    }

    int ret = -1;

    // If we are already in fallback mode and we still do not have a block, try to assign with several types
    // (Old direct fallback routes are built into useFallbackBuffer above)

    // VITA_DEBUG_LOG("[Video][DECODE] call fmt=%s w=%u h=%u strideBytes=%u pitchPx=%u phys=%s tex=%p staging=%s",
    //     decodeYuv ? "YUV420" : "RGBA",
    //     picture.frame.frameWidth,
    //     picture.frame.frameHeight,
    //     strideBytes,
    //     picture.frame.framePitch,
    //     decoder_use_phys_fallback ? "yes" : "no",
    //     texBack,
    //     useLinearStaging ? "yes" : "no");
    uint64_t start_dec = sceKernelGetSystemTimeWide();
    ret                = sceAvcdecDecode(decoder, &au, &array_picture);
    uint64_t end_dec   = sceKernelGetSystemTimeWide();
    if (ret >= 0)
    {
        uint32_t dec_ms = (uint32_t)(end_dec - start_dec) / 1000;
        g_stats.decode_time_ms += dec_ms;
        if (dec_ms < g_decode_min_ms)
            g_decode_min_ms = dec_ms;
        if (dec_ms > g_decode_max_ms)
            g_decode_max_ms = dec_ms;
        g_decode_sum_ms += dec_ms;
        g_decode_count++;
    }

    // Map common errors for quick diagnosis
    const char* errName = "OK";
    switch ((uint32_t)ret)
    {
        case SCE_AVCDEC_ERROR_UNSUPPORT_IMAGE_SIZE:
            errName = "UNSUPPORT_IMAGE_SIZE";
            break;
        case SCE_AVCDEC_ERROR_INVALID_COLOR_FORMAT:
            errName = "INVALID_COLOR_FORMAT";
            break;
        case SCE_AVCDEC_ERROR_NOT_PHY_CONTINUOUS_MEMORY:
            errName = "NOT_PHY_CONT_MEM";
            break;
        case SCE_AVCDEC_ERROR_INVALID_POINTER:
            errName = "INVALID_POINTER";
            break;
    }
    // VITA_DEBUG_LOG("[Video][DECODE] ret=0x%x (%s) outputs=%d", ret, errName, array_picture.numOfOutput);
    if (ret == SCE_AVCDEC_ERROR_UNSUPPORT_IMAGE_SIZE)
    {
        VITA_DEBUG_LOG("[Video][ERR] UNSUPPORT_IMAGE_SIZE w=%u h=%u pitch=%u -> DR_NEED_IDR", picture.frame.frameWidth, picture.frame.frameHeight, picture.frame.framePitch);
        return DR_NEED_IDR;
    }

    static unsigned syntheticFrameIndex = 0;
    vita_netopt_on_frame_seen(syntheticFrameIndex);

    if (ret < 0 || vd_submit_counter < 4 || (vd_submit_counter % 240) == 0)
    {
        VITA_DEBUG_LOG("[Video] dec ret=0x%x out=%d pitch=%u fb=%s physPtr=%p", ret, array_picture.numOfOutput, picture.frame.framePitch, decoder_use_phys_fallback ? "on" : "off", decoder_output_phys_ptr);
    }
    if (ret < 0)
    {
        VITA_DEBUG_LOG("[Video] sceAvcdecDecode error: 0x%x (pf_mode=%d)", ret, decoder_output_mode);
        return DR_NEED_IDR;
    }

    if (array_picture.numOfOutput != 1)
    {
        // If there is no output but we are in physical fallback and we have a block, make a tentative copy (best-effort)
        // No output yet
        syntheticFrameIndex++;
        vd_submit_counter++;
        return DR_OK;
    }

    // Direct decode path: decoder writes directly to BACK texture.
    if (!decodeYuv)
    {
        cpuPushPtr        = decodeTarget;
        cpuPushPitchBytes = strideBytes;
    }

    static bool loggedFirstOut = false;
    if (!loggedFirstOut)
    {
        VITA_DEBUG_LOG("[Video] Primer frame output confirmado por decoder");
        loggedFirstOut = true;
    }
    // Copy from staging to texture if we use staging
    // Without staging: it is already in texture
    last_call_had_output = true;

    // (Already decoded directly into BACK texture)

    if (!single_frame_buffer)
    {
        int bufferMode = g_video_settings_snapshot.buffer_mode;
        if (bufferMode == 2)
        {
            // Triple-buffering lock-free swap via atomic exchange
            int current_write = __atomic_load_n(&frame_write_idx, __ATOMIC_ACQUIRE);
            int old_ready     = __atomic_exchange_n(&frame_ready_idx, current_write, __ATOMIC_SEQ_CST);
            __atomic_store_n(&frame_write_idx, old_ready, __ATOMIC_RELEASE);
            __atomic_store_n(&frame_ready_flag, true, __ATOMIC_RELEASE);

            static uint32_t decode_diag_counter = 0;
            if ((decode_diag_counter++ % 181) == 0)
            {
                VITA_DEBUG_LOG("[Video][DIAG] Triple Swap - Decoded to %d, Published to ready_idx=%d, Next write_idx=%d (Mode: Triple Exchange)",
                    current_write, current_write, old_ready);
            }
        }
        else
        {
            // Double-buffering rotation (0 -> 1 -> 0)
            int current_write = __atomic_load_n(&frame_write_idx, __ATOMIC_ACQUIRE);
            __atomic_store_n(&frame_ready_idx, current_write, __ATOMIC_RELEASE);
            int next_write = (current_write + 1) % 2;
            __atomic_store_n(&frame_write_idx, next_write, __ATOMIC_RELEASE);
            __atomic_store_n(&frame_ready_flag, true, __ATOMIC_RELEASE);

            static uint32_t decode_diag_counter = 0;
            if ((decode_diag_counter++ % 181) == 0)
            {
                VITA_DEBUG_LOG("[Video][DIAG] Double Swap - Decoded to %d, Published to ready_idx=%d, Next write_idx=%d (Mode: Double Rotation)",
                    current_write, current_write, next_write);
            }
        }
    }
    else
    {
        static uint32_t single_diag_counter = 0;
        if ((single_diag_counter++ % 181) == 0)
        {
            VITA_DEBUG_LOG("[Video][DIAG] Buffer Swap - Mode: Single (display=%d ready=%d write=%d)",
                frame_display_idx, frame_ready_idx, frame_write_idx);
        }
    }

    if (g_stats.frames_decoded == 0)
        VITA_DEBUG_LOG("[Video][DBG] primer frame decodificado");

    VitaSession::onFrameDecoded();

    uint64_t tPresentMs = monotonicMs_local();
    if (active_video_thread)
    {
        vita_netopt_frame_produced();
        if (need_drop > 0)
        {
            need_drop--;
            g_stats.frames_dropped_pacer++;
        }
        else
        {
            g_stats.frames_decoded++;
            frame_count++;
            vita_netopt_on_frame_completed(syntheticFrameIndex);
        }
    }
    syntheticFrameIndex++;
    vd_submit_counter++;
    // Case: consecutive outputs=1 in frames where host sends accumulated slices.
    // The API can return repeated outputs=1 if there are ready frames in the internal queue; we accept.
    return DR_OK;
}
