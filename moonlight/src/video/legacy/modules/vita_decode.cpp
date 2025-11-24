#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/videodec.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include <algorithm>

#include "vita_globals.hpp"
#ifdef __cplusplus
#include "libgamestream/sps.h"
extern gs::SpsContext* g_sps_ctx;
#endif
#include <borealis/extern/nanovg/nanovg.h>

#include <borealis/core/application.hpp>

#include "Limelight.h"
#include "debug.hpp"
#include "network/NetworkOptimizations.hpp"
#include "session/vita_session.hpp"
#include "video/VideoFrameHolder.hpp"
#include "video/render_mode_cache.hpp"
#include "vita_sceAvcInternal.hpp"

typedef struct ScePafInit
{
    SceSize global_heap_size;
    int a2;
    int a3;
    int cdlg_mode;
    int heap_opt_param1;
    int heap_opt_param2;
} ScePafInit;

// Función para inicializar soporte 1080p con API Internal
// Carga módulos PAF/AVCDEC y configura el decoder para resoluciones > 720p
int vitavideo_init_1080p_internal_api(int width, int height, SceVideodecQueryInitInfoHwAvcdec* init)
{
    static bool modules_loaded = false;

    // Solo cargar módulos una vez
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

        // Configuración Internal
        int ret_config = sceVideodecSetConfigInternal(SCE_VIDEODEC_TYPE_HW_AVCDEC, 2);
        VITA_DEBUG_LOG("[Video][1080p] sceVideodecSetConfigInternal: 0x%x", ret_config);

        int ret_mode = sceAvcdecSetDecodeModeInternal(SCE_VIDEODEC_TYPE_HW_AVCDEC, SCE_AVCDEC_MODE_EXTENDED);
        VITA_DEBUG_LOG("[Video][1080p] sceAvcdecSetDecodeModeInternal: 0x%x", ret_mode);

        modules_loaded = true;
    }

    // Inicializar con API Internal
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

// Externs ya están en vita_globals.hpp

#ifndef SCE_AVCDEC_PIXELFORMAT_YUV420_PLANAR
#define SCE_AVCDEC_PIXELFORMAT_YUV420_PLANAR 0x4
#endif

static inline uint64_t monotonicMs_local()
{
    if (LiGetMillis)
        return LiGetMillis();
    return 0;
}

// Control interno para evitar spam de logs en fallback físico
static bool decoder_phys_fallback_permanent_disable = false; // si true no volver a intentar alloc físico
static int decoder_phys_fallback_fail_count         = 0;

static inline uint8_t clamp_byte(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return (uint8_t)value;
}

static void convert_yuv420_to_rgba(const uint8_t* yPlane, const uint8_t* uPlane, const uint8_t* vPlane,
    uint32_t width, uint32_t height,
    uint32_t yPitch, uint32_t uvPitch,
    uint8_t* dst, uint32_t dstStrideBytes)
{
    if (!yPlane || !uPlane || !vPlane || !dst || !yPitch || !uvPitch || !dstStrideBytes)
    {
        return;
    }

    for (uint32_t row = 0; row < height; ++row)
    {
        const uint8_t* yRow = yPlane + (size_t)yPitch * row;
        const uint8_t* uRow = uPlane + (size_t)uvPitch * (row / 2);
        const uint8_t* vRow = vPlane + (size_t)uvPitch * (row / 2);
        uint8_t* out        = dst + (size_t)dstStrideBytes * row;

        for (uint32_t col = 0; col < width; ++col)
        {
            int C = (int)yRow[col] - 16;
            if (C < 0)
                C = 0;
            int D = (int)uRow[col / 2] - 128;
            int E = (int)vRow[col / 2] - 128;

            int R = (298 * C + 409 * E + 128) >> 8;
            int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
            int B = (298 * C + 516 * D + 128) >> 8;

            out[4 * col + 0] = clamp_byte(B);
            out[4 * col + 1] = clamp_byte(G);
            out[4 * col + 2] = clamp_byte(R);
            out[4 * col + 3] = 0xFF;
        }
    }
}

extern "C" int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit)
{
    static uint32_t vd_submit_counter            = 0;
    static bool last_call_had_output             = false;
    static const uint64_t texture_guard_sig_tail = 0xA55AA55AA55AA55AULL;
    static const uint64_t texture_guard_sig_head = 0xDEADBEEFCAFEBABEULL;
    static const uint8_t texture_guard_fill      = 0xD6;
    // Destino actual (single buffer) => FRAME_BACK() == FRAME_FRONT() mientras single_frame_buffer=true
    uint8_t* texBack         = (uint8_t*)vita2d_texture_get_datap(FRAME_BACK());
    unsigned int strideBytes = vita2d_texture_get_stride(FRAME_BACK());
    if (!strideBytes)
        strideBytes = image_scaling.texture_width * 4;
    // Forzar ausencia de fallback físico en modo RGBA (depuración estable)
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
    if (decodeYuv && (!decoder_yuv_buffer || decoder_yuv_buffer_size == 0))
    {
        VITA_DEBUG_LOG("[Video][WARN] Buffer YUV no disponible, volviendo a RGBA");
        decoder_output_mode = 0;
        decodeYuv           = false;
    }
    bool useLinearStaging = false;
    if (!decodeYuv && decoder_linear_rgba && decoder_linear_rgba_physically_backed && decoder_linear_rgba_pitch_pixels != 0 && decoder_linear_rgba_height != 0)
    {
        size_t expectedBytes = (size_t)decoder_linear_rgba_pitch_pixels * 4 * (size_t)decoder_linear_rgba_height;
        if (decoder_linear_rgba_size >= expectedBytes)
        {
            useLinearStaging = true;
        }
    }

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
    picture.frame.pixelType               = decodeYuv ? SCE_AVCDEC_PIXELFORMAT_YUV420_RASTER : SCE_AVCDEC_PIXELFORMAT_RGBA8888;
    picture.frame.frameWidth              = alignedW;
    picture.frame.horizontalSize          = alignedW;
    picture.frame.frameCropLeftOffset     = 0;
    picture.frame.frameCropRightOffset    = 0;
    picture.frame.frameCropTopOffset      = 0;
    picture.frame.frameCropBottomOffset   = 0;
    picture.frame.opt.rgba.alpha          = 0xFF;
    picture.frame.opt.rgba.cscCoefficient = 0;

    // Destino directo: textura FRONT (single buffer) o BACK (si más adelante se reactiva doble buffer)
    // Para single_frame_buffer ambos índices apuntan al mismo.
    bool decodeUsesFallback = false;
    uint8_t* decodeTarget   = nullptr;
    if (decodeYuv)
    {
        decodeTarget = decoder_yuv_buffer;
    }
    else if (useFallbackBuffer && fallbackPtr)
    {
        decodeTarget       = fallbackPtr;
        decodeUsesFallback = true;
    }
    else if (useLinearStaging)
    {
        decodeTarget = decoder_linear_rgba;
    }
    else
    {
        decodeTarget = texBack;
    }
    if (!decodeTarget && texBack)
    {
        VITA_DEBUG_LOG("[Video][WARN] Destino YUV inválido, usando textura RGBA");
        decodeTarget            = texBack;
        decodeYuv               = false;
        decoder_output_mode     = 0;
        picture.frame.pixelType = SCE_AVCDEC_PIXELFORMAT_RGBA8888;
        useLinearStaging        = false;
    }
    if (!decodeTarget)
    {
        VITA_DEBUG_LOG("[Video][ERR] No hay destino válido para decodificar (decodeYuv=%d)", decodeYuv ? 1 : 0);
        return DR_NEED_IDR;
    }
    uint32_t frameHeightForDecoder = baseH;
    if (decodeYuv)
    {
        frameHeightForDecoder = baseH;
    }
    else if (useLinearStaging)
    {
        frameHeightForDecoder = decoder_linear_rgba_height ? decoder_linear_rgba_height : baseH;
    }
    else if (decodeUsesFallback)
    {
        frameHeightForDecoder = baseH;
    }
    // FIX: Forzar 1088 si baseH es 1088 (evitar mismatch con SPS parcheado)
    if (baseH == 1088 && frameHeightForDecoder == 1080)
    {
        frameHeightForDecoder = 1088;
    }

    picture.frame.frameHeight  = frameHeightForDecoder;
    picture.frame.verticalSize = frameHeightForDecoder;

    uint32_t framePitchPixels = 0;
    if (decodeYuv)
    {
        framePitchPixels = alignedW;
    }
    else if (decodeUsesFallback)
    {
        framePitchPixels = fallbackPitchPixels ? fallbackPitchPixels : alignedW;
    }
    else if (useLinearStaging)
    {
        framePitchPixels = decoder_linear_rgba_pitch_pixels;
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

    uint8_t* guardBasePtr    = nullptr;
    uint8_t* guardTailPtr    = nullptr;
    size_t guardInteriorSize = 0;
    if (useLinearStaging && decoder_linear_rgba_guard && decoder_linear_rgba_guard_size >= (2 * sizeof(uint64_t)))
    {
        guardBasePtr      = decoder_linear_rgba_guard;
        guardTailPtr      = guardBasePtr + decoder_linear_rgba_guard_size - sizeof(uint64_t);
        guardInteriorSize = decoder_linear_rgba_guard_size - (2 * sizeof(uint64_t));
        if (guardInteriorSize > 0)
        {
            memset(guardBasePtr + sizeof(uint64_t), texture_guard_fill, guardInteriorSize);
        }
        *(uint64_t*)guardBasePtr = texture_guard_sig_head;
        *(uint64_t*)guardTailPtr = texture_guard_sig_tail;
        if (decoder_linear_rgba_memblock >= 0)
        {
            int syncRes = sceKernelSyncVMDomain(decoder_linear_rgba_memblock, guardBasePtr, (SceSize)decoder_linear_rgba_guard_size);
            if (syncRes < 0)
            {
                VITA_DEBUG_LOG("[Video][WARN] Sync guard write failed: 0x%08X", syncRes);
            }
        }
    }

    const uint8_t* cpuPushPtr  = nullptr;
    uint32_t cpuPushPitchBytes = 0;

    picture.frame.pPicture[0] = decodeTarget; // decodificar directo al buffer elegido
    // (Fase1) Push diferido al FINAL tras swap (similar a Switch: se expone frame estable)
    picture.frame.pPicture[1] = NULL; // siempre NULL

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

    // Si ya estamos en modo fallback y aún no tenemos bloque, intentar asignar con varios tipos
    // (Las rutas antiguas de fallback directo se integran arriba en useFallbackBuffer)

    // VITA_DEBUG_LOG("[Video][DECODE] call fmt=%s w=%u h=%u strideBytes=%u pitchPx=%u phys=%s tex=%p staging=%s",
    //     decodeYuv ? "YUV420" : "RGBA",
    //     picture.frame.frameWidth,
    //     picture.frame.frameHeight,
    //     strideBytes,
    //     picture.frame.framePitch,
    //     decoder_use_phys_fallback ? "yes" : "no",
    //     texBack,
    //     useLinearStaging ? "yes" : "no");
    ret = sceAvcdecDecode(decoder, &au, &array_picture);
    if (guardBasePtr)
    {
        if (decoder_linear_rgba_memblock >= 0)
        {
            int syncRes = sceKernelSyncVMDomain(decoder_linear_rgba_memblock, guardBasePtr, (SceSize)decoder_linear_rgba_guard_size);
            if (syncRes < 0)
            {
                VITA_DEBUG_LOG("[Video][WARN] Sync guard check failed: 0x%08X", syncRes);
            }
        }
        bool headOk            = (*(uint64_t*)guardBasePtr == texture_guard_sig_head);
        bool tailOk            = (*(uint64_t*)guardTailPtr == texture_guard_sig_tail);
        size_t firstCorruption = SIZE_MAX;
        if (guardInteriorSize > 0)
        {
            const uint8_t* interior = guardBasePtr + sizeof(uint64_t);
            for (size_t i = 0; i < guardInteriorSize; ++i)
            {
                if (interior[i] != texture_guard_fill)
                {
                    firstCorruption = i;
                    break;
                }
            }
        }
        if (!headOk || !tailOk || firstCorruption != SIZE_MAX)
        {
            size_t bytesIntoGuard = (firstCorruption == SIZE_MAX) ? 0 : (firstCorruption + 1);
            float rowsIntoGuard   = 0.0f;
            size_t rowBytes       = (size_t)decoder_linear_rgba_pitch_pixels * 4;
            if (rowBytes > 0)
            {
                rowsIntoGuard = (float)bytesIntoGuard / (float)rowBytes;
            }
            VITA_DEBUG_LOG("[Video][GUARD][ERR] Staging RGBA corrupta (frame #%u) head=%s tail=%s overrun=%luB (~%.2f filas)", vd_submit_counter,
                headOk ? "ok" : "bad",
                tailOk ? "ok" : "bad",
                (unsigned long)bytesIntoGuard,
                rowsIntoGuard);
        }
    }
    // Mapear errores comunes para diagnóstico rápido
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
        // Si no hay output pero estamos en fallback físico y tenemos bloque, hacer copia tentativa (best-effort)
        // Sin output todavía
        syntheticFrameIndex++;
        vd_submit_counter++;
        return DR_OK;
    }

    if (decodeYuv)
    {
        if (!yuv_check_canaries())
        {
            VITA_DEBUG_LOG("[Video][YUV][WARN] Canarios YUV dañados tras decode");
        }
        uint32_t framePitch         = picture.frame.framePitch ? picture.frame.framePitch : alignedW;
        uint32_t frameHeightAligned = picture.frame.frameHeight ? picture.frame.frameHeight : baseH;
        size_t yPlaneBytes          = (size_t)framePitch * (size_t)frameHeightAligned;
        uint32_t chromaPitch        = framePitch / 2;
        uint32_t chromaHeight       = frameHeightAligned / 2;
        size_t uvPlaneBytes         = (size_t)chromaPitch * (size_t)chromaHeight;
        size_t requiredBytes        = yPlaneBytes + uvPlaneBytes + uvPlaneBytes;
        if (decoder_yuv_buffer_size < requiredBytes)
        {
            VITA_DEBUG_LOG("[Video][YUV][ERR] Buffer YUV insuficiente (tiene=%lu necesita=%lu)", (unsigned long)decoder_yuv_buffer_size, (unsigned long)requiredBytes);
        }
        else if (!texBack)
        {
            VITA_DEBUG_LOG("[Video][YUV][ERR] Textura destino nula, omitiendo conversión");
        }
        else
        {
            uint32_t outWidth  = decoder_src_width > 0 ? (uint32_t)decoder_src_width : alignedW;
            uint32_t outHeight = decoder_src_height > 0 ? (uint32_t)decoder_src_height : baseH;
            if (outWidth > framePitch)
                outWidth = framePitch;
            if (outHeight > frameHeightAligned)
                outHeight = frameHeightAligned;
            if (outWidth == 0 || outHeight == 0)
            {
                VITA_DEBUG_LOG("[Video][YUV][WARN] Dimensiones inválidas %ux%u para conversión", outWidth, outHeight);
            }
            else
            {
                const uint8_t* yPlane        = decoder_yuv_buffer;
                const uint8_t* uPlane        = yPlane + yPlaneBytes;
                const uint8_t* vPlane        = uPlane + uvPlaneBytes;
                static bool loggedCpuConvert = false;
                if (!loggedCpuConvert)
                {
                    VITA_DEBUG_LOG("[Video][YUV] Convirtiendo YUV420 -> RGBA en CPU (%ux%u)", outWidth, outHeight);
                    loggedCpuConvert = true;
                }
                convert_yuv420_to_rgba(yPlane, uPlane, vPlane,
                    outWidth, outHeight,
                    framePitch, chromaPitch,
                    texBack, strideBytes);
                cpuPushPtr        = texBack;
                cpuPushPitchBytes = strideBytes;
            }
        }
        yuv_write_canaries();
    }
    else if (useLinearStaging && texBack)
    {
        uint32_t effectivePitchPx = decoder_linear_rgba_pitch_pixels ? decoder_linear_rgba_pitch_pixels : alignedW;
        size_t stagingPitchBytes  = (size_t)effectivePitchPx * 4;
        if (stagingPitchBytes > 0)
        {
            uint32_t copyWidth = decoder_src_width > 0 ? (uint32_t)decoder_src_width : alignedW;
            if (copyWidth > effectivePitchPx)
                copyWidth = effectivePitchPx;
            uint32_t copyHeight       = decoder_src_height > 0 ? (uint32_t)decoder_src_height : baseH;
            uint32_t maxHeightByPitch = decoder_linear_rgba_height ? decoder_linear_rgba_height : baseH;
            size_t maxRowsBySize      = stagingPitchBytes ? (decoder_linear_rgba_size / stagingPitchBytes) : 0;
            if (maxRowsBySize > 0 && maxRowsBySize < maxHeightByPitch)
            {
                maxHeightByPitch = (uint32_t)maxRowsBySize;
            }
            if (copyHeight > maxHeightByPitch)
                copyHeight = maxHeightByPitch;
            size_t copyRowBytes     = (size_t)copyWidth * 4;
            size_t stagingBytesUsed = stagingPitchBytes * copyHeight;
            if (stagingBytesUsed > decoder_linear_rgba_size)
            {
                stagingBytesUsed = decoder_linear_rgba_size;
            }
            if (copyRowBytes > 0 && copyHeight > 0 && stagingBytesUsed > 0)
            {
                if (decoder_linear_rgba_memblock >= 0)
                {
                    int syncRes = sceKernelSyncVMDomain(decoder_linear_rgba_memblock, decoder_linear_rgba, stagingBytesUsed);
                    if (syncRes < 0)
                    {
                        VITA_DEBUG_LOG("[Video][WARN] Sync staging read failed: 0x%08X", syncRes);
                    }
                }
                for (uint32_t row = 0; row < copyHeight; ++row)
                {
                    memcpy(texBack + (size_t)strideBytes * row,
                        decoder_linear_rgba + stagingPitchBytes * row,
                        copyRowBytes);
                }
                cpuPushPtr        = decoder_linear_rgba;
                cpuPushPitchBytes = (uint32_t)stagingPitchBytes;
            }
        }
    }
    else if (decodeUsesFallback && fallbackPtr && texBack)
    {
        uint32_t effectivePitchPx = fallbackPitchPixels ? fallbackPitchPixels : alignedW;
        size_t fallbackPitchBytes = (size_t)effectivePitchPx * 4;
        if (fallbackPitchBytes > 0)
        {
            uint32_t copyWidth = decoder_src_width > 0 ? (uint32_t)decoder_src_width : alignedW;
            if (copyWidth > effectivePitchPx)
                copyWidth = effectivePitchPx;
            uint32_t copyHeight       = decoder_src_height > 0 ? (uint32_t)decoder_src_height : baseH;
            uint32_t maxHeightByPitch = frameHeightForDecoder ? frameHeightForDecoder : baseH;
            if (copyHeight > maxHeightByPitch)
                copyHeight = maxHeightByPitch;
            size_t copyRowBytes      = (size_t)copyWidth * 4;
            size_t fallbackBytesUsed = fallbackPitchBytes * copyHeight;
            if (fallbackBytesUsed > 0)
            {
                if (decoder_output_phys_block >= 0)
                {
                    int syncRes = sceKernelSyncVMDomain(decoder_output_phys_block, fallbackPtr, (SceSize)fallbackBytesUsed);
                    if (syncRes < 0)
                    {
                        VITA_DEBUG_LOG("[Video][FB_PHY] Sync fallback read failed: 0x%08X", syncRes);
                    }
                }
                for (uint32_t row = 0; row < copyHeight; ++row)
                {
                    memcpy(texBack + (size_t)strideBytes * row,
                        fallbackPtr + fallbackPitchBytes * row,
                        copyRowBytes);
                }
                cpuPushPtr        = fallbackPtr;
                cpuPushPitchBytes = (uint32_t)fallbackPitchBytes;
            }
        }
    }

    static bool loggedFirstOut = false;
    if (!loggedFirstOut)
    {
        VITA_DEBUG_LOG("[Video] Primer frame output confirmado por decoder");
        loggedFirstOut = true;
    }
    // Copiar del staging a la textura si usamos staging
    // Sin staging: ya está en textura
    last_call_had_output = true;

    // (Ya decodificado directamente en textura BACK)

    if (!single_frame_buffer)
    {
        int tmp         = frame_front_idx;
        frame_front_idx = frame_back_idx;
        frame_back_idx  = tmp;
        // VITA_DEBUG_LOG("[Video][DECODE] swap buffers front=%d back=%d", frame_front_idx, frame_back_idx);
    }

    // Marcar primer frame (log previo a incrementar frames_decoded)
    if (g_stats.frames_decoded == 0)
        VITA_DEBUG_LOG("[Video][DBG] primer frame decodificado");

    // Publicar textura directamente (sin copias CPU)
    {
        const vita2d_texture* texFront = FRAME_FRONT();
        const uint32_t w               = image_scaling.texture_width;
        const uint32_t h               = image_scaling.texture_height;
        if (!texFront)
        {
            VITA_DEBUG_LOG("[Video][DECODE][WARN] FRAME_FRONT null tras swap (front=%d)", frame_front_idx);
        }
        else if (w == 0 || h == 0)
        {
            VITA_DEBUG_LOG("[Video][DECODE][WARN] dimensiones invalidas para publicar tex=%p w=%u h=%u",
                texFront,
                w,
                h);
        }
        else
        {
            // VITA_DEBUG_LOG("[Video][DECODE] publicar frame front=%d tex=%p w=%u h=%u (mode=%s pitchBytes=%u)",
            //     frame_front_idx,
            //     texFront,
            //     w,
            //     h,
            //     cpuPushPtr ? "cpu" : "tex",
            //     cpuPushPitchBytes);
            VideoFrameHolder::instance().pushTexture(texFront, w, h, monotonicMs_local());
        }
    }
    VitaSession::onFrameDecoded();

    // Modo Direct GXM eliminado: no se sube frame a renderer directo

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
    // (primer frame ya marcado arriba)
    syntheticFrameIndex++;
    vd_submit_counter++;
    // Caso: outputs=1 consecutivos en frames donde host manda slices acumuladas.
    // La API puede devolver outputs=1 repetido si hay frames listos en cola interna; aceptamos.
    return DR_OK;
}

// g_sps_ctx se define como puntero crudo en vita_globals.cpp
