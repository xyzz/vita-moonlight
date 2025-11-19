#include <malloc.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/videodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include <algorithm>
#include <borealis/core/application.hpp>
#include <memory>

#include "network/NetworkOptimizations.hpp"
#include "vita_globals.hpp"
// #include "libgamestream/sps.h" // deshabilitado (SPS context temporalmente fuera)

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

// Definir constantes que pueden faltar
#ifndef SCE_VIDEODEC_TYPE_HW_AVCDEC
#define SCE_VIDEODEC_TYPE_HW_AVCDEC ((SceVideodecType)0x1001)
#endif

int vita_pacer_thread_main(SceSize args, void* argp);
extern "C" void vita_cleanup();
uint64_t vita_monotonic_ms();

extern "C" int vitavideo_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags)
{
    (void)videoFormat;
    (void)redrawRate;
    (void)context;
    (void)drFlags; // de momento no usados en esta ruta simplificada
    int ret = 0;
    // Registrar target FPS si es válido (>0)
    if (redrawRate > 0)
    {
        g_stats.target_fps = (uint32_t)redrawRate;
    }
    // Paso 1: framebuffer y buffers iniciales
    if (video_status == VITA_VIDEO_NOT_INIT)
    {
        if (!vita2d_inited)
        {
            vita2d_init();
            vita2d_inited = true;
            vita2d_set_vblank_wait(0); // desactivar espera de vblank para baja latencia
        }

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

        vitavideo_update_scaling_settings(width, height); // define image_scaling basándose en resolución original

        uint32_t alignedW      = VITA_DECODER_RESOLUTION(width);
        uint32_t alignedH      = VITA_DECODER_RESOLUTION(height);
        size_t rowBytesAligned = (size_t)alignedW * 4;

        // Preparar staging RGBA antes de crear las texturas
        decoder_src_width            = width;
        decoder_src_height           = height;
        decoder_tried_direct_texture = false;
        decoder_use_phys_fallback    = false; // intentar primero decode directo a textura; activar fallback dinámico si falla
        decoder_output_mode          = g_video_settings_snapshot.pixel_format_mode;
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
            VITA_DEBUG_LOG("[Video][INIT] Modo Pixel=YUV420 (experimental, conversión CPU)");
            uint32_t alignedW       = VITA_DECODER_RESOLUTION(width);
            uint32_t alignedH       = VITA_DECODER_RESOLUTION(height);
            size_t yPlaneBytes      = (size_t)alignedW * (size_t)alignedH;
            size_t uvPlaneBytes     = (size_t)(alignedW / 2) * (size_t)(alignedH / 2);
            size_t payloadBytes     = yPlaneBytes + uvPlaneBytes + uvPlaneBytes;
            const size_t guardBytes = 32;
            decoder_yuv_raw         = (uint8_t*)malloc(payloadBytes + guardBytes);
            if (!decoder_yuv_raw)
            {
                VITA_DEBUG_LOG("[Video][ERR] No memoria para buffer YUV (%lu bytes). Revertemos a RGBA", (unsigned long)(payloadBytes + guardBytes));
                decoder_output_mode = 0;
            }
            else
            {
                memset(decoder_yuv_raw, 0, payloadBytes + guardBytes);
                decoder_yuv_buffer      = decoder_yuv_raw + (guardBytes / 2);
                decoder_yuv_buffer_size = payloadBytes;
                decoder_yuv_total_alloc = payloadBytes + guardBytes;
                yuv_write_canaries();
            }
        }

        if (decoder_output_mode == 0)
        {
            VITA_DEBUG_LOG("[Video][INIT] Modo Pixel=RGBA directo");
            decoder_use_phys_fallback = false; // probar directo primero

            // Reinicializar staging RGBA
            if (decoder_linear_rgba_memblock >= 0)
            {
                sceKernelFreeMemBlock(decoder_linear_rgba_memblock);
                decoder_linear_rgba_memblock = -1;
            }
            else if (decoder_linear_rgba)
            {
                free(decoder_linear_rgba);
            }
            decoder_linear_rgba                   = nullptr;
            decoder_linear_rgba_size              = 0;
            decoder_linear_rgba_guard             = nullptr;
            decoder_linear_rgba_guard_size        = 0;
            decoder_linear_rgba_total_alloc       = 0;
            decoder_linear_rgba_pitch_pixels      = 0;
            decoder_linear_rgba_height            = 0;
            decoder_linear_rgba_physically_backed = false;

            size_t requiredBytes                = rowBytesAligned * alignedH;
            constexpr size_t kGuardFloor        = 0x20000; // 128 KiB mínimo
            constexpr size_t kGuardRows         = 128; // filas extra sobre pitch
            constexpr size_t kMemblockAlignment = 256 * 1024; // requerido para PHYCONT/CDRAM
            size_t guardBytesRequested          = std::max<size_t>(rowBytesAligned * kGuardRows, kGuardFloor);
            size_t totalBytesRequested          = requiredBytes + guardBytesRequested;
            size_t totalAllocBytes              = align_up_size(totalBytesRequested, kMemblockAlignment);
            if (totalAllocBytes < totalBytesRequested)
            {
                totalAllocBytes = totalBytesRequested; // alineamiento falló, conserva tamaño mínimo
            }
            size_t guardBytes = (totalAllocBytes > requiredBytes) ? (totalAllocBytes - requiredBytes) : guardBytesRequested;

            uint8_t* linearBuf                        = nullptr;
            SceKernelMemBlockType stagingMemblockType = (SceKernelMemBlockType)0;
            if (requiredBytes > 0)
            {
                const SceKernelMemBlockType types[] = {
                    SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                    SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
                    SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW
                };
                SceKernelAllocMemBlockOpt opt;
                memset(&opt, 0, sizeof(opt));
                opt.size      = sizeof(opt);
                opt.attr      = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
                opt.alignment = 0x1000;
                for (unsigned ti = 0; ti < sizeof(types) / sizeof(types[0]) && !linearBuf; ++ti)
                {
                    SceUID blk = sceKernelAllocMemBlock("dec_rgba_staging", types[ti], (SceSize)totalAllocBytes, &opt);
                    if (blk < 0)
                    {
                        VITA_DEBUG_LOG("[Video][WARN] memblock staging type=0x%X err=0x%08X", types[ti], blk);
                        continue;
                    }
                    void* base  = nullptr;
                    int baseRes = sceKernelGetMemBlockBase(blk, &base);
                    if (baseRes >= 0 && base)
                    {
                        decoder_linear_rgba_memblock = blk;
                        linearBuf                    = static_cast<uint8_t*>(base);
                        stagingMemblockType          = types[ti];
                    }
                    else
                    {
                        VITA_DEBUG_LOG("[Video][WARN] GetMemBlockBase staging err=0x%08X", baseRes);
                        sceKernelFreeMemBlock(blk);
                    }
                }
            }

            if (!linearBuf && requiredBytes > 0)
            {
                linearBuf = (uint8_t*)memalign(0x1000, totalAllocBytes);
                if (!linearBuf)
                {
                    linearBuf = (uint8_t*)malloc(totalAllocBytes);
                }
                if (linearBuf)
                {
                    VITA_DEBUG_LOG("[Video][WARN] Staging RGBA usando heap no físico (%u bytes)", (unsigned)totalAllocBytes);
                }
            }

            if (!linearBuf)
            {
                VITA_DEBUG_LOG("[Video][ERR] No se pudo asignar staging RGBA (%lu bytes)", (unsigned long)totalAllocBytes);
            }
            else
            {
                decoder_linear_rgba                   = linearBuf;
                decoder_linear_rgba_size              = requiredBytes;
                decoder_linear_rgba_total_alloc       = totalAllocBytes;
                decoder_linear_rgba_guard_size        = guardBytes;
                decoder_linear_rgba_guard             = decoder_linear_rgba + decoder_linear_rgba_size;
                decoder_linear_rgba_pitch_pixels      = alignedW;
                decoder_linear_rgba_height            = alignedH;
                decoder_linear_rgba_physically_backed = (decoder_linear_rgba_memblock >= 0);
                memset(decoder_linear_rgba, 0, totalAllocBytes);
                if (decoder_linear_rgba_memblock >= 0)
                {
                    int syncRes = sceKernelSyncVMDomain(decoder_linear_rgba_memblock, decoder_linear_rgba, (SceSize)totalAllocBytes);
                    if (syncRes < 0)
                    {
                        VITA_DEBUG_LOG("[Video][WARN] sceKernelSyncVMDomain staging init: 0x%08X", syncRes);
                    }
                }
                VITA_DEBUG_LOG("[Video][INIT] Buffer staging RGBA asignado (%ux%u pitchBytes=%u backing=%s type=0x%X guard=%lu total=%lu)",
                    alignedW,
                    alignedH,
                    (unsigned)rowBytesAligned,
                    decoder_linear_rgba_physically_backed ? "físico" : "heap",
                    stagingMemblockType,
                    (unsigned long)guardBytes,
                    (unsigned long)totalAllocBytes);
                if (!decoder_linear_rgba_physically_backed)
                {
                    VITA_DEBUG_LOG("[Video][WARN] Staging RGBA no es físico; se omitirá staging durante decode");
                }
            }
        }

        // Crear doble buffer de texturas RGBA para evitar race decode/GPU
        auto prevTexMemType = vita2d_texture_get_alloc_memblock_type();
        vita2d_texture_set_alloc_memblock_type(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW);
        bool texturesOk = true;
        for (int i = 0; i < 2; i++)
        {
            frame_textures[i] = vita2d_create_empty_texture_format(
                width,
                height,
                SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
            if (!frame_textures[i])
            {
                VITA_DEBUG_LOG("[Video] Error: No hay memoria para frame_texture %d", i);
                ret        = 0x80010001;
                texturesOk = false;
                break;
            }
            uint8_t* datap = (uint8_t*)vita2d_texture_get_datap(frame_textures[i]);
            if (datap)
                memset(datap, 0, (size_t)width * (size_t)height * 4);
        }
        vita2d_texture_set_alloc_memblock_type(prevTexMemType);
        if (!texturesOk)
        {
            goto cleanup;
        }
        frame_front_idx                 = 0;
        frame_back_idx                  = 1;
        single_frame_buffer             = false;
        legacy_single_immediate_present = false;
        VITA_DEBUG_LOG("[Video][INIT] double buffer RGBA (front=%d back=%d)", frame_front_idx, frame_back_idx);
        VITA_DEBUG_LOG("[Video][INIT] tex0=%p tex1=%p", vita2d_texture_get_datap(frame_textures[0]), frame_textures[1] ? vita2d_texture_get_datap(frame_textures[1]) : nullptr);

        VITA_DEBUG_LOG("[Video][INIT] Pipeline inicial listo (zero-copy removido)");
        VITA_DEBUG_LOG("[Video] Framebuffer inicializado");
        video_status = VITA_VIDEO_INIT_FRAMEBUFFER;
    }
    if (video_status == VITA_VIDEO_INIT_FRAMEBUFFER)
    {
        // Low latency removido: la ruta inmediata es siempre usada (comentario informativo)
        VITA_DEBUG_LOG("[Video][SETUP] Ruta inmediata activa (low latency eliminado)");
        video_fullscreen_stretch = g_video_settings_snapshot.fullscreen;
        // Configurar pacing target fps si se conoce
        if (g_stats.target_fps == 0 && redrawRate > 0)
        {
            g_stats.target_fps = (uint32_t)redrawRate;
        }
        vita_netopt_set_target_fps(g_stats.target_fps ? g_stats.target_fps : 60);
// Crear contexto SPS (fix) si no existe
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
        // No need for RGBA buffer, decoding directly to texture
        // SPS context deshabilitado temporalmente
        ret = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, init);
        if (ret < 0)
        {
            VITA_DEBUG_LOG("[Video] Error sceVideodecInitLibrary: 0x%x", ret);
            ret = 0x80010002;
            goto cleanup;
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
        ret                                   = sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder_info, &decoder_info_out);
        if (ret < 0)
        {
            VITA_DEBUG_LOG("[Video] Error sceAvcdecQueryDecoderMemSize: 0x%x", ret);
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
        decoderblock           = sceKernelAllocMemBlock("decoder", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, sz, NULL);
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
        video_status = VITA_VIDEO_INIT_DECODER_MEMBLOCK;
    }
    if (video_status == VITA_VIDEO_INIT_DECODER_MEMBLOCK)
    {
        VITA_DEBUG_LOG("[Video] Creando decoder AVC...");
        ret = sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder, decoder_info);
        if (ret < 0)
        {
            VITA_DEBUG_LOG("[Video] Error sceAvcdecCreateDecoder: 0x%x", ret);
            ret = 0x80010006;
            goto cleanup;
        }
        video_status = VITA_VIDEO_INIT_AVC_DEC;
    }
    if (video_status == VITA_VIDEO_INIT_AVC_DEC)
    {
        ret = sceKernelCreateThread("frame_pacer", vita_pacer_thread_main, 0, 0x10000, 0, 0, NULL);
        if (ret < 0)
        {
            VITA_DEBUG_LOG("[Video] Error creando thread frame_pacer: 0x%x", ret);
            ret = 0x80010007;
            goto cleanup;
        }
        pacer_thread        = ret;
        active_pacer_thread = true;
        sceKernelStartThread(pacer_thread, 0, NULL);
        // Ajustar afinidad del hilo principal (este thread que hace setup) para dejar un core libre al decoder/renderer
        SceUID selfId = sceKernelGetThreadId();
        sceKernelChangeThreadCpuAffinityMask(selfId, SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_2);
        video_status = VITA_VIDEO_INIT_FRAME_PACER_THREAD;
    }
    // Configuración inicial: direct output ON, pure copy ON, sin síntesis de start codes (fallback los activará si es necesario)
    // Modos legacy eliminados: no se requiere configuración adicional
    VITA_DEBUG_LOG("[Video] vitavideo_setup completado exitosamente, retornando 0");
    if (video_status_canary_pre != 0xDEADBEEF || video_status_canary_post != 0xBAADF00D)
    {
        VITA_DEBUG_LOG("[Video][CORRUPT] Canarios video_status alterados despues de setup pre=0x%08X post=0x%08X", video_status_canary_pre, video_status_canary_post);
    }
    return 0;
cleanup:
    if (ret != 0 && vita2d_inited)
    { // si falló setup parcial, liberar
        vita2d_fini();
        vita2d_inited = false;
    }
    vita_cleanup();
    return ret;
}
