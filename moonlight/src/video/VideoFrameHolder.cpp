#include "video/VideoFrameHolder.hpp"

#include <psp2/gxm.h>
#include <vita2d.h>

#include <atomic>
#include <mutex>

#include "legacy/modules/vita_globals.hpp"

VideoFrameHolder& VideoFrameHolder::instance()
{
    static VideoFrameHolder inst;
    return inst;
}

void VideoFrameHolder::pushTexture(const vita2d_texture* texture, uint32_t w, uint32_t h, uint64_t ptsMs)
{
    if (!texture || w == 0 || h == 0)
    {
        VITA_DEBUG_LOG("[VideoFrameHolder][pushTex] textura invalida tex=%p w=%u h=%u", texture, w, h);
        return;
    }

    const SceGxmTexture* gxmTex = &texture->gxm_tex;
    if (!gxmTex || !sceGxmTextureGetData(const_cast<SceGxmTexture*>(gxmTex)))
    {
        VITA_DEBUG_LOG("[VideoFrameHolder][pushTex][WARN] textura sin datos gxm=%p", gxmTex);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    latest_.texture    = texture;
    latest_.gxmTexture = gxmTex;
    latest_.w          = w;
    latest_.h          = h;
    latest_.ptsMs      = ptsMs;

    hasNew_.store(true, std::memory_order_release);
    framesPushed_.fetch_add(1, std::memory_order_relaxed);
    // VITA_DEBUG_LOG("[VideoFrameHolder][pushTex] listo tex=%p size=%ux%u framesPushed=%llu",
    //     texture,
    //     w,
    //     h,
    //     (unsigned long long)framesPushed_.load(std::memory_order_relaxed));
}

bool VideoFrameHolder::popLatest(GxmFrame& out)
{
    if (!hasNew_.load(std::memory_order_acquire))
    {
        VITA_DEBUG_LOG("[VideoFrameHolder][pop] sin frame nuevo");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasNew_.load(std::memory_order_relaxed))
    {
        VITA_DEBUG_LOG("[VideoFrameHolder][pop] bandera limpia durante lock");
        return false;
    }

    out = latest_;
    hasNew_.store(false, std::memory_order_release);
    framesPopped_.fetch_add(1, std::memory_order_relaxed);
    VITA_DEBUG_LOG("[VideoFrameHolder][pop] entrega tex=%p w=%u h=%u framesPopped=%llu",
        out.texture,
        out.w,
        out.h,
        (unsigned long long)framesPopped_.load(std::memory_order_relaxed));
    return true;
}

void VideoFrameHolder::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.texture    = nullptr;
    latest_.gxmTexture = nullptr;
    latest_.w          = 0;
    latest_.h          = 0;
    latest_.ptsMs      = 0;
    hasNew_.store(false, std::memory_order_release);
}
