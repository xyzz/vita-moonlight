#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
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
#include "session/vita_session.hpp"
extern "C"
{
#include "Platform.h"
}
#include "vita_sceAvcInternal.hpp"

extern "C" void vita_video_frame_published(void);

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

extern "C" int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit)
{
    static uint32_t vd_submit_counter = 0;
    if (!decodeUnit)
    {
        video_stats_record_corruption(true);
        return DR_NEED_IDR;
    }

    video_stats_record_input_frame(decodeUnit);
    if (video_decode_backlog_requires_refresh(decodeUnit))
    {
        return DR_NEED_IDR;
    }
    int currentWrite = __atomic_load_n(&frame_write_idx, __ATOMIC_ACQUIRE);
    video_frame_wait_for_slot(currentWrite);
    GxmTexture* writeTexture = __atomic_load_n(&frame_textures[currentWrite], __ATOMIC_ACQUIRE);
    uint8_t* texBack         = writeTexture ? (uint8_t*)gxm_texture_get_datap(writeTexture) : nullptr;
    unsigned int strideBytes = writeTexture ? gxm_texture_get_stride(writeTexture) : 0;
    if (!strideBytes)
        strideBytes = image_scaling.texture_width * 4;
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

    uint8_t* decodeTarget   = nullptr;
    picture.frame.pixelType = decodeYuv ? SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER : SCE_AVCDEC_PIXELFORMAT_RGBA8888;
    decodeTarget            = texBack;

    if (!decodeTarget)
    {
        VITA_DEBUG_LOG("[Video][ERR] No valid decode target (modo=%d)", decoder_output_mode);
        return DR_NEED_IDR;
    }

    picture.frame.frameWidth     = alignedW;
    picture.frame.horizontalSize = alignedW;
    picture.frame.frameHeight    = baseH;
    picture.frame.verticalSize   = baseH;

    uint32_t framePitchPixels = 0;
    if (decodeYuv)
    {
        // YUV raster decode path uses aligned width in pixels as pitch.
        framePitchPixels = alignedW;
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

    picture.frame.pPicture[0] = decodeTarget; // decode directly to the chosen buffer
    if (decodeYuv)
    {
        picture.frame.pPicture[1] = decodeTarget + (alignedW * baseH);
    }
    else
    {
        picture.frame.pPicture[1] = NULL;
    }

    if (decodeUnit->fullLength <= 0 || !decodeUnit->bufferList)
    {
        video_stats_record_corruption(true);
        return DR_NEED_IDR;
    }

    size_t rawLength = 0;
    for (PLENTRY current = decodeUnit->bufferList; current; current = current->next)
    {
        if (!current->data || current->length <= 0 || static_cast<size_t>(current->length) > SIZE_MAX - rawLength)
        {
            video_stats_record_corruption(true);
            return DR_NEED_IDR;
        }
        rawLength += static_cast<size_t>(current->length);
    }
    if (rawLength != static_cast<size_t>(decodeUnit->fullLength))
    {
        video_stats_record_corruption(true);
        return DR_NEED_IDR;
    }

    const size_t requiredCapacity = static_cast<size_t>(decodeUnit->fullLength) + AV_INPUT_BUFFER_PADDING_SIZE;
    if (decoder_buffer_size < requiredCapacity)
    {
        char* resizedBuffer = static_cast<char*>(realloc(decoder_buffer, requiredCapacity));
        if (!resizedBuffer)
        {
            return DR_NEED_IDR;
        }
        decoder_buffer      = resizedBuffer;
        decoder_buffer_size = requiredCapacity;
    }
    PLENTRY entry          = decodeUnit->bufferList;
    size_t length          = 0;
    const auto appendEntry = [&](PLENTRY current)
    {
        if (!current || !current->data || current->length <= 0 || length > decoder_buffer_size)
        {
            return false;
        }
        const size_t entryLength = static_cast<size_t>(current->length);
        if (entryLength > decoder_buffer_size - length)
        {
            return false;
        }
        memcpy(decoder_buffer + length, current->data, entryLength);
        length += entryLength;
        return true;
    };

    while (entry)
    {
        bool appended = false;
        if (entry->bufferType == BUFFER_TYPE_SPS)
        {
#ifdef __cplusplus
            if (g_sps_ctx)
            {
                size_t before = length;
                appended      = g_sps_ctx->fix(entry, GS_SPS_BITSTREAM_FIXUP,
                         reinterpret_cast<uint8_t*>(decoder_buffer),
                         decoder_buffer_size, &length);
                if (appended)
                {
                    VITA_DEBUG_LOG("[Video][SPS] Fix aplicado (in=%u out=%u)",
                        entry->length, (unsigned)(length - before));
                }
            }
            else
#endif
            {
                appended = appendEntry(entry);
            }
        }
        else
        {
            appended = appendEntry(entry);
        }
        if (!appended)
        {
            video_stats_record_corruption(length != static_cast<size_t>(decodeUnit->fullLength));
            VITA_DEBUG_LOG("[Video] AU assembly exceeded buffer capacity");
            return DR_NEED_IDR;
        }
        entry = entry->next;
    }
    au.es.pBuf   = decoder_buffer;
    au.es.size   = static_cast<uint32_t>(length);
    au.dts.lower = au.dts.upper = au.pts.lower = au.pts.upper = 0xFFFFFFFF;

    if (vd_submit_counter < 2)
    {
        VITA_DEBUG_LOG("[Video] AU size=%u fullLength=%u", au.es.size, decodeUnit->fullLength);
    }

    int ret            = -1;
    uint64_t start_dec = sceKernelGetSystemTimeWide();
    ret                = sceAvcdecDecode(decoder, &au, &array_picture);
    uint64_t end_dec   = sceKernelGetSystemTimeWide();
    if (ret >= 0)
    {
        uint32_t dec_ms = (uint32_t)(end_dec - start_dec) / 1000;
        video_stats::add(g_stats.decode_time_ms, dec_ms);
        video_stats::updateMinimum(g_decode_min_ms, dec_ms);
        video_stats::updateMaximum(g_decode_max_ms, dec_ms);
        video_stats::add(g_decode_sum_ms, dec_ms);
        video_stats::increment(g_decode_count);
    }

    if (ret == SCE_AVCDEC_ERROR_UNSUPPORT_IMAGE_SIZE)
    {
        video_stats_record_corruption(false);
        VITA_DEBUG_LOG("[Video][ERR] UNSUPPORT_IMAGE_SIZE w=%u h=%u pitch=%u -> DR_NEED_IDR", picture.frame.frameWidth, picture.frame.frameHeight, picture.frame.framePitch);
        return DR_NEED_IDR;
    }

    if (ret < 0 || vd_submit_counter < 4 || (vd_submit_counter % 240) == 0)
    {
        VITA_DEBUG_LOG("[Video] dec ret=0x%x out=%d pitch=%u", ret, array_picture.numOfOutput, picture.frame.framePitch);
    }
    if (ret < 0)
    {
        video_stats_record_corruption(false);
        VITA_DEBUG_LOG("[Video] sceAvcdecDecode error: 0x%x (pf_mode=%d)", ret, decoder_output_mode);
        return DR_NEED_IDR;
    }

    if (array_picture.numOfOutput != 1)
    {
        vd_submit_counter++;
        return DR_OK;
    }

    static bool loggedFirstOut = false;
    if (!loggedFirstOut)
    {
        VITA_DEBUG_LOG("[Video] Primer frame output confirmado por decoder");
        loggedFirstOut = true;
    }
    int nextWrite = (currentWrite + 1) % 3;
    __atomic_store_n(&frame_write_idx, nextWrite, __ATOMIC_RELEASE);

    static uint32_t rotationDiagCounter = 0;
    if ((rotationDiagCounter++ % 181) == 0)
    {
        VITA_DEBUG_LOG("[Video][DIAG] Latest frame %d published, next write %d", currentWrite, nextWrite);
    }

    video_frame_publish(currentWrite, (uint32_t)decodeUnit->frameNumber,
        (uint32_t)sceKernelGetProcessTimeLow());

    // Wake the render loop now instead of letting it sleep out its frame budget.
    vita_video_frame_published();

    if (video_stats::load(g_stats.frames_decoded) == 0)
        VITA_DEBUG_LOG("[Video][DBG] primer frame decodificado");

    VitaSession::onFrameDecoded();

    if (active_video_thread.load(std::memory_order_acquire))
    {
        video_stats::increment(g_stats.frames_decoded);
    }
    vd_submit_counter++;
    // Case: consecutive outputs=1 in frames where host sends accumulated slices.
    // The API can return repeated outputs=1 if there are ready frames in the internal queue; we accept.
    return DR_OK;
}
