#pragma once
// gxm_texture.hpp — Minimal GXM texture allocator
// Replaces vita2d_texture for video frame management.
// Uses Borealis's GXM context (no second sceGxmInitialize needed).

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
    uint32_t stride; // bytes per row (aligned)
    uint32_t data_size; // total allocated size
};

// Create a texture with the given format, allocated in the specified memory type.
// memType: SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW (GPU fast) or
//          SCE_KERNEL_MEMBLOCK_TYPE_USER_RW (main RAM)
static inline GxmTexture* gxm_texture_create(uint32_t w, uint32_t h,
    SceGxmTextureFormat fmt,
    SceKernelMemBlockType memType)
{
    // Calculate bytes per pixel based on format
    uint32_t bpp = 4; // default RGBA
    switch (fmt)
    {
        case SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR:
            bpp = 4;
            break;
        case SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0:
        case SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0:
            bpp = 1; // Y plane only, but total size = w*h*1.5
            break;
        default:
            bpp = 4;
            break;
    }

    // GXM requires width aligned to 8 pixels for linear textures
    uint32_t alignedW = (w + 7) & ~7;
    uint32_t stride   = alignedW * bpp;

    // Calculate total size
    uint32_t dataSize;
    if (fmt == SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0 || fmt == SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0)
    {
        // YUV420: Y plane (w*h) + UV planes (w*h/2)
        dataSize = alignedW * h * 3 / 2;
    }
    else
    {
        dataSize = stride * h;
    }

    // Align size to 256KB boundary (GXM requirement)
    uint32_t allocSize = (dataSize + 0x3FFFF) & ~0x3FFFF;
    if (allocSize < 0x40000)
        allocSize = 0x40000; // minimum 256KB

    // Allocate GPU memory
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

    // Map memory for GPU access
    SceGxmMemoryAttribFlags attribs = SCE_GXM_MEMORY_ATTRIB_RW;
    if (sceGxmMapMemory(base, allocSize, attribs) < 0)
    {
        sceKernelFreeMemBlock(uid);
        return nullptr;
    }

    // Allocate struct (CPU memory)
    GxmTexture* tex = (GxmTexture*)malloc(sizeof(GxmTexture));
    if (!tex)
    {
        sceGxmUnmapMemory(base);
        sceKernelFreeMemBlock(uid);
        return nullptr;
    }

    // Initialize the GXM texture object
    sceClibMemset(&tex->gxm_tex, 0, sizeof(SceGxmTexture));
    sceGxmTextureInitLinear(&tex->gxm_tex, base, fmt, w, h, 0);

    tex->mem_uid   = uid;
    tex->width     = w;
    tex->height    = h;
    tex->stride    = stride;
    tex->data_size = allocSize;

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

#endif // __PSV__
