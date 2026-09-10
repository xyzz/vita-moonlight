#include <malloc.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <borealis/core/application.hpp>
#include <memory>

#include "video/pixel_format/pixel_format.hpp"
#include "video/runtime_settings.hpp"
#include "vita_globals.hpp"
#include "vita_sceAvcInternal.hpp"

// External pixel processor from vita_decode.cpp
extern PixelFormat::IPixelProcessor* g_pixelProcessor;

static inline size_t align_up_size(size_t value, size_t alignment)
{
    if (alignment == 0)
    {
        return value;
    }
    size_t remainder = value % alignment;
    if (remainder == 0)
    {
        return value;
    }
    size_t aligned = value + (alignment - remainder);
    if (aligned < value)
    {
        return value; // overflow guard, fall back to original size
    }
    return aligned;
}

#ifndef SCE_VIDEODEC_TYPE_HW_AVCDEC
#define SCE_VIDEODEC_TYPE_HW_AVCDEC ((SceVideodecType)0x1001)
#endif

extern "C" void vita_cleanup();
uint64_t vita_monotonic_ms();

extern "C" int vitavideo_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags)
{
    (void)videoFormat;
    (void)redrawRate;
    (void)context;
    (void)drFlags; // currently not used on this simplified route
    int ret = 0;
    if (redrawRate > 0)
    {
        video_stats::store(g_stats.target_fps, (uint32_t)redrawRate);
    }
    if (video_status == VITA_VIDEO_NOT_INIT)
    {
        vitavideo_reset_stats();
        // Ensure we register a one-time application-exit hook that will
        // perform the full teardown of vita resources when Borealis exits.
        // This prevents calling full teardown (sceVideodecTermLibrary / vita2d_fini)
        // while the UI is still active, which can cause driver hangs.
        decoder_buffer_size = DECODER_BUFFER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE;
        decoder_buffer      = (char*)malloc(decoder_buffer_size);
        if (!decoder_buffer)
        {
            VITA_DEBUG_LOG("[Video] Error: No hay memoria para decoder_buffer");
            ret = 0x80010001;
            goto cleanup;
        }

        vitavideo_update_scaling_settings(width, height); // define image_scaling based on original resolution

        decoder_src_width            = width;
        decoder_src_height           = height;
        decoder_tried_direct_texture = false;
        decoder_use_phys_fallback    = false; // try first decode direct to texture; activate dynamic fallback if it fails
        decoder_output_mode          = video_runtime::pixelFormatMode();
        decoder_yuv_raw              = nullptr;
        decoder_yuv_buffer           = nullptr;
        decoder_yuv_buffer_size      = 0;
        decoder_yuv_total_alloc      = 0;
        if (decoder_output_mode != 0 && decoder_output_mode != 1)
        {
            VITA_DEBUG_LOG("[Video][INIT] pixel_format_mode desconocido=%d -> forzando RGBA", decoder_output_mode);
            decoder_output_mode = 0;
        }

        if (decoder_output_mode == 1)
        {
            VITA_DEBUG_LOG("[Video][INIT] Modo de procesado: YUV GPU CSC (Alto Rendimiento)");
        }
        else
        {
            VITA_DEBUG_LOG("[Video][INIT] Modo de procesado: RGBA Hardware Direct (No Downscale)");
        }

        video_fullscreen_stretch = video_runtime::fullscreen();
        if (video_stats::load(g_stats.target_fps) == 0 && redrawRate > 0)
        {
            video_stats::store(g_stats.target_fps, (uint32_t)redrawRate);
        }
#ifdef __cplusplus
        if (!g_sps_ctx)
        {
            g_sps_ctx = new gs::SpsContext(width, height);
            if (g_sps_ctx)
                VITA_DEBUG_LOG("[Video][SPS] Contexto SPS inicializado (%dx%d)", width, height);
        }
#endif
        if (!init)
        {
            init = (SceVideodecQueryInitInfoHwAvcdec*)calloc(1, sizeof(*init));
            if (!init)
            {
                ret = 0x80010001;
                goto cleanup;
            }
        }
        init->size           = sizeof(*init);
        init->horizontal     = VITA_DECODER_RESOLUTION(width);
        init->vertical       = VITA_DECODER_RESOLUTION(height);
        init->numOfRefFrames = 4;
        init->numOfStreams   = 1;
        decoder_width        = init->horizontal;
        decoder_height       = init->vertical;

        int low_delay_ret = sceAvcdecSetLowDelayModeNongameapp(
            SCE_VIDEODEC_TYPE_HW_AVCDEC,
            SCE_AVCDEC_DELAY_MODE_LOW);
        VITA_DEBUG_LOG(
            "[Video][LOW_DELAY] sceAvcdecSetLowDelayModeNongameapp(mode=%d, refs=%u): 0x%08x",
            SCE_AVCDEC_DELAY_MODE_LOW,
            init->numOfRefFrames,
            low_delay_ret);

        if (width > 1280 || height > 720)
        {
            VITA_DEBUG_LOG("[Video] Detectada resolución > 720p, usando API Internal");
            ret = vitavideo_init_1080p_internal_api(width, height, init);
            if (ret < 0)
            {
                VITA_DEBUG_LOG("[Video] Error en init 1080p: 0x%x", ret);
                ret = 0x80010002;
                goto cleanup;
            }
        }
        else
        {
            ret = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, init);
            if (ret < 0)
            {
                VITA_DEBUG_LOG("[Video] Error sceVideodecInitLibrary: 0x%x", ret);
                ret = 0x80010002;
                goto cleanup;
            }
        }

        video_status = VITA_VIDEO_INIT_AVC_LIB;
    }
    if (video_status == VITA_VIDEO_INIT_AVC_LIB)
    {
        if (!decoder_info)
        {
            decoder_info = (SceAvcdecQueryDecoderInfo*)calloc(1, sizeof(*decoder_info));
            if (!decoder_info)
            {
                ret = 0x80010001;
                goto cleanup;
            }
        }
        decoder_info->horizontal              = init->horizontal;
        decoder_info->vertical                = init->vertical;
        decoder_info->numOfRefFrames          = init->numOfRefFrames;
        SceAvcdecDecoderInfo decoder_info_out = { 0 };

        if (width > 1280 || height > 720)
        {
            ret = sceAvcdecQueryDecoderMemSizeInternal(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder_info, &decoder_info_out);
        }
        else
        {
            ret = sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder_info, &decoder_info_out);
        }

        if (ret < 0)
        {
            VITA_DEBUG_LOG("[Video] Error QueryDecoderMemSize: 0x%x", ret);
            ret = 0x80010003;
            goto cleanup;
        }
        decoder = (SceAvcdecCtrl*)calloc(1, sizeof(SceAvcdecCtrl));
        if (!decoder)
        {
            ret = 0x80010001;
            goto cleanup;
        }
        size_t sz              = (decoder_info_out.frameMemSize + 0xFFFFF) & ~0xFFFFF;
        decoder->frameBuf.size = sz;

        // Reallocating this block fragments CDRAM between sessions.
        if (decoderblock >= 0)
        {
            if (decoder_block_size < sz)
            {
                VITA_DEBUG_LOG("[Video] Decoder existente insuficiente (size=%uMB req=%uMB), liberando...",
                    (unsigned)(decoder_block_size >> 20), (unsigned)(sz >> 20));
                sceKernelFreeMemBlock(decoderblock);
                decoderblock       = -1;
                decoder_block_size = 0;
            }
            else
            {
                ret = sceKernelGetMemBlockBase(decoderblock, &decoder->frameBuf.pBuf);
                if (ret >= 0 && decoder->frameBuf.pBuf)
                {
                    VITA_DEBUG_LOG("[Video] Reutilizando decoder existente (blk=0x%X size=%uMB)", decoderblock, (unsigned)(decoder_block_size >> 20));
                }
                else
                {
                    sceKernelFreeMemBlock(decoderblock);
                    decoderblock       = -1;
                    decoder_block_size = 0;
                }
            }
        }

        if (decoderblock < 0)
        {
            // The 1080p decoder does not fit in PHYCONT.
            SceKernelMemBlockType decoderMemType = (width > 1280 || height > 720) ? SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW : SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW;
            const char* memTypeName              = (width > 1280 || height > 720) ? "CDRAM" : "PHYCONT";
            VITA_DEBUG_LOG("[Video] Asignando decoder %uMB en %s", (unsigned)(sz >> 20), memTypeName);

            decoderblock = sceKernelAllocMemBlock("decoder", decoderMemType, sz, NULL);
            if (decoderblock < 0)
            {
                VITA_DEBUG_LOG("[Video] Error decoderblock: 0x%08x", decoderblock);
                ret = 0x80010004;
                goto cleanup;
            }
            ret = sceKernelGetMemBlockBase(decoderblock, &decoder->frameBuf.pBuf);
            if (ret < 0)
            {
                VITA_DEBUG_LOG("[Video] Error sceKernelGetMemBlockBase: 0x%x", ret);
                ret = 0x80010005;
                goto cleanup;
            }
            decoder_block_size = sz;
        }
        video_status = VITA_VIDEO_INIT_DECODER_MEMBLOCK;
    }
    if (video_status == VITA_VIDEO_INIT_DECODER_MEMBLOCK)
    {
        VITA_DEBUG_LOG("[Video] Creando decoder AVC...");

        if (width > 1280 || height > 720)
        {
            ret = sceAvcdecCreateDecoderInternal(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder, decoder_info);
        }
        else
        {
            ret = sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder, decoder_info);
        }

        if (ret < 0)
        {
            VITA_DEBUG_LOG("[Video] Error CreateDecoder: 0x%x", ret);
            ret = 0x80010006;
            goto cleanup;
        }

        bool texturesOk      = true;
        bool reusingTextures = false;

        uint32_t decoderPixelType         = (decoder_output_mode == 1)
                    ? SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER
                    : SCE_AVCDEC_PIXELFORMAT_RGBA8888;
        SceGxmTextureFormat textureFormat = SCE_GXM_TEXTURE_FORMAT_A8B8G8R8;
        if (decoderPixelType == SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER)
        {
            textureFormat = SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0;
        }

        if (frame_textures[0] && frame_textures[1] && frame_textures[2])
        {
            uint32_t tex0_w = gxm_texture_get_width(frame_textures[0]);
            uint32_t tex0_h = gxm_texture_get_height(frame_textures[0]);
            uint32_t tex1_w = gxm_texture_get_width(frame_textures[1]);
            uint32_t tex1_h = gxm_texture_get_height(frame_textures[1]);
            uint32_t tex2_w = gxm_texture_get_width(frame_textures[2]);
            uint32_t tex2_h = gxm_texture_get_height(frame_textures[2]);

            if (tex0_w == (unsigned)width && tex0_h == (unsigned)height && tex1_w == (unsigned)width && tex1_h == (unsigned)height && tex2_w == (unsigned)width && tex2_h == (unsigned)height && frame_textures[0]->format == textureFormat && frame_textures[1]->format == textureFormat && frame_textures[2]->format == textureFormat)
            {
                reusingTextures = true;
                VITA_DEBUG_LOG("[Video] Reutilizando texturas existentes (%ux%u format=0x%08X)", width, height, (unsigned)textureFormat);
            }
            else
            {
                VITA_DEBUG_LOG("[Video] Liberando texturas incompatibles (size=%ux%u, format=0x%08X)",
                    tex0_w, tex0_h, (unsigned)frame_textures[0]->format);
                for (int i = 0; i < 3; i++)
                {
                    if (frame_textures[i])
                    {
                        gxm_texture_free(frame_textures[i]);
                        frame_textures[i] = nullptr;
                    }
                }
            }
        }

        if (!reusingTextures)
        {
            for (int i = 0; i < 3; i++)
            {
                frame_textures[i] = gxm_texture_create(
                    width, height,
                    textureFormat,
                    SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW);
                if (!frame_textures[i])
                {
                    VITA_DEBUG_LOG("[Video][ERR] No se pudo crear textura %d (%dx%d)", i, width, height);
                    ret = 0x80010005;
                    goto cleanup;
                }
            }
            VITA_DEBUG_LOG("[Video][INIT] Creadas 3 texturas visible=%dx%d storage=%ux%u fmt=0x%08X",
                width, height,
                gxm_texture_get_storage_width(frame_textures[0]),
                gxm_texture_get_storage_height(frame_textures[0]),
                (unsigned)textureFormat);

            if (!texturesOk)
            {
                goto cleanup;
            }
        }

        frame_display_idx = 0;
        frame_write_idx   = 1;
        VITA_DEBUG_LOG("[Video][INIT] latest-frame rotation (display=%d write=%d)", frame_display_idx, frame_write_idx);
        VITA_DEBUG_LOG("[Video][INIT] tex0=%p tex1=%p tex2=%p", gxm_texture_get_datap(frame_textures[0]), gxm_texture_get_datap(frame_textures[1]), gxm_texture_get_datap(frame_textures[2]));
        VITA_DEBUG_LOG("[Video] Framebuffer inicializado");

        video_status = VITA_VIDEO_INIT_AVC_DEC;
    }
    VITA_DEBUG_LOG("[Video] vitavideo_setup completado exitosamente, retornando 0");
    if (video_status_canary_pre != 0xDEADBEEF || video_status_canary_post != 0xBAADF00D)
    {
        VITA_DEBUG_LOG("[Video][CORRUPT] Canarios video_status alterados despues de setup pre=0x%08X post=0x%08X", video_status_canary_pre, video_status_canary_post);
    }
    return 0;
cleanup:
    vita_cleanup();
    return ret;
}
