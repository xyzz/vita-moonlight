#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <borealis/core/application.hpp>

#include "debug.hpp"

extern "C" void ffmpeg_video_watchdog_tick(void);

namespace
{

brls::VoidEvent::Subscription s_runLoopSub;
bool s_subscribed = false;
bool s_active     = false;

// A decode callback may race session teardown, so keep the semaphore alive.
std::atomic<SceUID> s_frameSema { -1 };

uint32_t s_frameBudgetUs = 0;

void pace_callback()
{
    if (!s_active)
        return;

    SceUID sema = s_frameSema.load(std::memory_order_acquire);
    if (sema < 0)
        return;

    // Decoder publication drives normal rendering. The timeout only keeps UI
    // input and animations responsive if the stream stalls.
    SceUInt timeout = (SceUInt)(s_frameBudgetUs * 2);
    sceKernelWaitSema(sema, 1, &timeout);
    ffmpeg_video_watchdog_tick();
}

} // namespace

extern "C"
{

    void vita_video_frame_published(void)
    {
        SceUID sema = s_frameSema.load(std::memory_order_acquire);
        if (sema >= 0)
        {
            sceKernelSignalSema(sema, 1);
        }
    }

    void vita_frame_pacer_start(int paceFps)
    {
        if (s_active || paceFps <= 0)
            return;

        if (s_frameSema.load(std::memory_order_acquire) < 0)
        {
            SceUID sema = sceKernelCreateSema("video_frame", 0, 0, 1, nullptr);
            if (sema < 0)
            {
                vita_log::error("[FramePacer] sceKernelCreateSema failed: 0x%08X", sema);
                return;
            }
            s_frameSema.store(sema, std::memory_order_release);
        }

        s_frameBudgetUs = (uint32_t)(1000000 / paceFps);

        brls::Application::setLimitedFPS(0);

        s_runLoopSub = brls::Application::getRunLoopEvent()->subscribe(pace_callback);
        s_subscribed = true;
        s_active     = true;

        vita_log::info("[FramePacer] active: frame-driven wakeup, target=%d fps (idle=%u us)",
            paceFps, s_frameBudgetUs * 2);
    }

    void vita_frame_pacer_stop(int restoreFps)
    {
        if (!s_active)
            return;

        s_active = false;

        if (s_subscribed)
        {
            brls::Application::getRunLoopEvent()->unsubscribe(s_runLoopSub);
            s_subscribed = false;
        }

        brls::Application::setLimitedFPS(restoreFps > 0 ? (size_t)restoreFps : 0);

        vita_log::info("[FramePacer] stopped (borealis limiter restored to %d fps)", restoreFps);
    }

} // extern "C"
