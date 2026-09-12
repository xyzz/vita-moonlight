#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <borealis/core/application.hpp>

#include "debug.hpp"
#include "video/legacy/modules/vita_globals.hpp"

extern "C" void ffmpeg_video_watchdog_tick(void);

namespace
{

brls::VoidEvent::Subscription s_runLoopSub;
bool s_subscribed = false;
bool s_active     = false;

// A decode callback may race session teardown, so keep the semaphore alive.
std::atomic<SceUID> s_frameSema { -1 };
std::atomic<SceUID> s_watchdogThread { -1 };
std::atomic<bool> s_watchdogActive { false };

uint32_t s_frameBudgetUs = 0;

int fence_watchdog_main(SceSize, void*)
{
    while (s_watchdogActive.load(std::memory_order_acquire))
    {
        video_frame_watchdog_tick();
        sceKernelDelayThread(10000);
    }
    return 0;
}

void start_fence_watchdog()
{
    if (s_watchdogThread.load(std::memory_order_acquire) >= 0)
        return;

    SceUID thread = sceKernelCreateThread("video_fence_watch", fence_watchdog_main,
        0x10000114, 0x4000, 0, 0, nullptr);
    if (thread < 0)
    {
        vita_log::error("[FramePacer] fence watchdog creation failed: 0x%08X", thread);
        return;
    }

    s_watchdogActive.store(true, std::memory_order_release);
    s_watchdogThread.store(thread, std::memory_order_release);
    int result = sceKernelStartThread(thread, 0, nullptr);
    if (result < 0)
    {
        s_watchdogActive.store(false, std::memory_order_release);
        s_watchdogThread.store(-1, std::memory_order_release);
        sceKernelDeleteThread(thread);
        vita_log::error("[FramePacer] fence watchdog start failed: 0x%08X", result);
    }
    else
    {
        vita_log::info("[FramePacer] fence watchdog active");
    }
}

void stop_fence_watchdog()
{
    s_watchdogActive.store(false, std::memory_order_release);
    SceUID thread = s_watchdogThread.exchange(-1, std::memory_order_acq_rel);
    if (thread >= 0)
    {
        sceKernelWaitThreadEnd(thread, nullptr, nullptr);
        sceKernelDeleteThread(thread);
    }
}

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
        if (paceFps <= 0)
            return;

        s_frameBudgetUs = (uint32_t)(1000000 / paceFps);

        if (s_active)
        {
            brls::Application::setLimitedFPS(0);
            vita_log::info("[FramePacer] target updated: %d fps (idle=%u us)",
                paceFps, s_frameBudgetUs * 2);
            return;
        }

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

        brls::Application::setLimitedFPS(0);

        s_runLoopSub = brls::Application::getRunLoopEvent()->subscribe(pace_callback);
        s_subscribed = true;
        s_active     = true;
        start_fence_watchdog();

        vita_log::info("[FramePacer] active: frame-driven wakeup, target=%d fps (idle=%u us)",
            paceFps, s_frameBudgetUs * 2);
    }

    void vita_frame_pacer_stop(int restoreFps)
    {
        if (!s_active)
            return;

        s_active = false;
        stop_fence_watchdog();

        if (s_subscribed)
        {
            brls::Application::getRunLoopEvent()->unsubscribe(s_runLoopSub);
            s_subscribed = false;
        }

        brls::Application::setLimitedFPS(restoreFps > 0 ? (size_t)restoreFps : 0);

        vita_log::info("[FramePacer] stopped (borealis limiter restored to %d fps)", restoreFps);
    }

} // extern "C"
