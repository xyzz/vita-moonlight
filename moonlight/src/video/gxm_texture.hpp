#pragma once

#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)

#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <stdint.h>
#include <stdlib.h>

struct GxmTexture
{
    SceGxmTexture gxm_tex;
    SceUID mem_uid;
    uint32_t width;
    uint32_t height;
    uint32_t storage_width;
    uint32_t storage_height;
    uint32_t stride;
    uint32_t data_size;
    SceGxmTextureFormat format;
};

static inline GxmTexture* gxm_texture_create(uint32_t w, uint32_t h,
    SceGxmTextureFormat fmt,
    SceKernelMemBlockType memType)
{
    uint32_t bpp = 4;
    switch (fmt)
    {
        case SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR:
            bpp = 4;
            break;
        case SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0:
        case SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0:
            bpp = 1;
            break;
        default:
            bpp = 4;
            break;
    }

    uint32_t widthAlignment = fmt == SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0 ? 32 : 16;
    uint32_t alignedW       = (w + widthAlignment - 1) & ~(widthAlignment - 1);
    uint32_t alignedH       = (h + 15) & ~15;
    uint32_t stride         = alignedW * bpp;

    uint32_t dataSize;
    if (fmt == SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0 || fmt == SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0)
    {
        dataSize = alignedW * alignedH * 3 / 2;
    }
    else
    {
        dataSize = stride * alignedH;
    }

    // GXM allocations must be aligned to 256 KiB.
    uint32_t allocSize = (dataSize + 0x3FFFF) & ~0x3FFFF;
    if (allocSize < 0x40000)
        allocSize = 0x40000;

    SceUID uid = sceKernelAllocMemBlock("gxm_tex",
        memType,
        allocSize,
        NULL);
    if (uid < 0)
    {
        return nullptr;
    }

    void* base = nullptr;
    if (sceKernelGetMemBlockBase(uid, &base) < 0 || !base)
    {
        sceKernelFreeMemBlock(uid);
        return nullptr;
    }

    SceGxmMemoryAttribFlags attribs = SCE_GXM_MEMORY_ATTRIB_RW;
    if (sceGxmMapMemory(base, allocSize, attribs) < 0)
    {
        sceKernelFreeMemBlock(uid);
        return nullptr;
    }

    GxmTexture* tex = (GxmTexture*)malloc(sizeof(GxmTexture));
    if (!tex)
    {
        sceGxmUnmapMemory(base);
        sceKernelFreeMemBlock(uid);
        return nullptr;
    }

    sceClibMemset(&tex->gxm_tex, 0, sizeof(SceGxmTexture));
    if (sceGxmTextureInitLinear(&tex->gxm_tex, base, fmt, alignedW, alignedH, 0) < 0)
    {
        free(tex);
        sceGxmUnmapMemory(base);
        sceKernelFreeMemBlock(uid);
        return nullptr;
    }

    tex->mem_uid        = uid;
    tex->width          = w;
    tex->height         = h;
    tex->storage_width  = alignedW;
    tex->storage_height = alignedH;
    tex->stride         = stride;
    tex->data_size      = allocSize;
    tex->format         = fmt;

    return tex;
}

static inline void gxm_texture_free(GxmTexture* tex)
{
    if (!tex)
        return;
    void* data = sceGxmTextureGetData(&tex->gxm_tex);
    if (data)
    {
        sceGxmUnmapMemory(data);
    }
    if (tex->mem_uid >= 0)
    {
        sceKernelFreeMemBlock(tex->mem_uid);
    }
    free(tex);
}

static inline void* gxm_texture_get_datap(const GxmTexture* tex)
{
    if (!tex)
        return nullptr;
    return sceGxmTextureGetData(const_cast<SceGxmTexture*>(&tex->gxm_tex));
}

static inline uint32_t gxm_texture_get_stride(const GxmTexture* tex)
{
    if (!tex)
        return 0;
    return tex->stride;
}

static inline uint32_t gxm_texture_get_width(const GxmTexture* tex)
{
    if (!tex)
        return 0;
    return tex->width;
}

static inline uint32_t gxm_texture_get_height(const GxmTexture* tex)
{
    if (!tex)
        return 0;
    return tex->height;
}

static inline uint32_t gxm_texture_get_storage_width(const GxmTexture* tex)
{
    if (!tex)
        return 0;
    return tex->storage_width;
}

static inline uint32_t gxm_texture_get_storage_height(const GxmTexture* tex)
{
    if (!tex)
        return 0;
    return tex->storage_height;
}

#endif // __PSV__
