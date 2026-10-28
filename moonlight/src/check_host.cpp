/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#include <atomic>
#include <cstdio>
#include <thread>

#include "../../../third_party/mdnsniff/udp_sniffer_vita.h"
#include "../../../third_party/mdnsniff/udp_sniffer_win.h"
#include "debug.hpp"
#if defined(__PSV__) || defined(_WIN32)
#include <borealis/core/thread.hpp>
#include <borealis/core/view.hpp>

#include "tab/add_host_tab.hpp"
#include "tab/hosts_tab.hpp"
#endif
#if defined(__PSV__)
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#endif

namespace check_host
{

#if defined(__PSV__)
static SceUID vitaDiscoveryThread = -1;
static std::atomic<int> vitaDiscoveryStatus { 0 }; // 0: idle, 1: running, 2: finished

void stopVitaDiscovery()
{
    vitaDiscoveryStatus = 2;
    if (vitaDiscoveryThread > 0)
    {
        SceUInt timeout = 100 * 1000;
        sceKernelWaitThreadEnd(vitaDiscoveryThread, NULL, &timeout);
        sceKernelDeleteThread(vitaDiscoveryThread);
        vitaDiscoveryThread = -1;
    }
    udp_sniffer_vita_deinit();
}

extern "C"
{
#if defined(__PSV__)
#include <psp2/kernel/clib.h>
#define MDNS_LOG(...) sceClibPrintf(__VA_ARGS__)
#else
#define MDNS_LOG(...) printf(__VA_ARGS__)
#endif
}

namespace
{
    static void (*vitaHostFoundCb)(int, const char*, const char*, const char*, int) = nullptr;
    extern "C" void vitaHostFoundCbLogger(int idx, const char* host, const char* pcname, const char* ip, int port)
    {
        MDNS_LOG("[mdns_log] CALLBACK VITA: idx=%d host=%s pcname=%s ip=%s port=%d\n", idx, host ? host : "(null)", pcname ? pcname : "(null)", ip ? ip : "(null)", port);
        vita_log::info("[check_host] CALLBACK VITA LLAMADO: idx=%d host=%s pcname=%s ip=%s port=%d", idx, host ? host : "(null)", pcname ? pcname : "(null)", ip ? ip : "(null)", port);
        if (vitaHostFoundCb)
            vitaHostFoundCb(idx, host, pcname, ip, port);
    }
}

void startVitaDiscovery(void (*hostFoundCb)(int, const char*, const char*, const char*, int))
{
    // Match the legacy client initialization order before starting discovery.
    vita_log::info("[check_host] startVitaDiscovery: starting (Legacy mode)");
#if defined(__PSV__)
    vita_log::info("[check_host] startVitaDiscovery: starting (Vita log)\n");
#endif
    stopVitaDiscovery();
    vita_log::info("[check_host] startVitaDiscovery: after stopVitaDiscovery");
    static void* net_mem        = nullptr;
    static bool net_initialized = false;
    if (!net_initialized)
    {
        int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
        if (ret < 0)
        {
            vita_log::error("[check_host] ERROR: sceSysmoduleLoadModule failed: 0x%08X", ret);
            return;
        }
        net_mem = malloc(256 * 1024);
        if (!net_mem)
        {
            vita_log::error("[check_host] ERROR: malloc for net_mem failed");
            return;
        }
        SceNetInitParam netInitParam;
        netInitParam.memory = net_mem;
        netInitParam.size   = 256 * 1024;
        netInitParam.flags  = 0;
        ret                 = sceNetInit(&netInitParam);
        if (ret < 0 && ret != 0x80410110)
        {
            vita_log::error("[check_host] ERROR: sceNetInit failed: 0x%08X", ret);
            free(net_mem);
            net_mem = nullptr;
            return;
        }
        ret = sceNetCtlInit();
        if (ret < 0 && ret != 0x80412110 && ret != 0x80412102)
        {
            char errbuf[64];
            snprintf(errbuf, sizeof(errbuf), "sceNetCtlInit failed: hex=0x%08X dec=%d", (unsigned int)ret, ret);
            std::string errMsg = std::string("[check_host] ERROR: ") + errbuf;
            vita_log::error("%s", errMsg.c_str());
            MDNS_LOG("[mdns_log] %s\n", errMsg.c_str());
            sceNetTerm();
            free(net_mem);
            net_mem = nullptr;
            return;
        }
        net_initialized = true;
        vita_log::info("[check_host] Vita network initialized (legacy path)");
    }
    vitaDiscoveryStatus = 1;
    udp_sniffer_vita_init();
    vita_log::info("[check_host] startVitaDiscovery: after udp_sniffer_vita_init");
#if defined(__PSV__)
    vita_log::info("[check_host] startVitaDiscovery: udp_sniffer_vita_init called\n");
#endif
    vitaHostFoundCb = hostFoundCb;
    vita_log::info("[check_host] startVitaDiscovery: vitaHostFoundCb assigned");
#if defined(__PSV__)
    vita_log::info("[check_host] startVitaDiscovery: vitaHostFoundCb set to %p\n", (void*)vitaHostFoundCb);
#endif
    udp_sniffer_vita_set_callback(vitaHostFoundCbLogger);
    vita_log::info("[check_host] startVitaDiscovery: after udp_sniffer_vita_set_callback");
#if defined(__PSV__)
    vita_log::info("[check_host] startVitaDiscovery: udp_sniffer_vita_set_callback called\n");
#endif
    vita_log::info("[check_host] startVitaDiscovery: creating thread");
#if defined(__PSV__)
    vita_log::info("[check_host] startVitaDiscovery: about to create discovery thread\n");
#endif
    vitaDiscoveryThread = sceKernelCreateThread("mdns_discovery", [](SceSize, void* argp) -> int
        {
    MDNS_LOG("[mdns_log] mDNS discovery started (up to 100 iterations or until the tab closes)\n");
    vita_log::info("[check_host] mDNS discovery started (up to 100 iterations or tab closure)");
#if defined(__PSV__)
    vita_log::debug("[mdns_log] Vita mDNS discovery thread started\n");
#endif
        int iter = 0;
        while (vitaDiscoveryStatus == 1 && iter < 100) {
            iter++;
            if (iter % 10 == 0) {
                MDNS_LOG("[mdns_log] mDNS discovery: iteration %d\n", iter);
                vita_log::info("[check_host] mDNS discovery: iteration %d", iter);
            }
            udp_sniffer_vita_poll();
            sceKernelDelayThread(1000 * 100); // 100ms
        }
        MDNS_LOG("[mdns_log] Saliendo del bucle de descubrimiento mDNS\n");
        vita_log::info("[check_host] Saliendo del bucle de descubrimiento mDNS");
        udp_sniffer_vita_deinit();
        MDNS_LOG("[mdns_log] Hilo de descubrimiento mDNS finalizado\n");
        vita_log::info("[check_host] mDNS discovery thread finished");
#if defined(__PSV__)
    vita_log::debug("[mdns_log] mDNS discovery thread finished (VITA)\n");
#endif
        vitaDiscoveryStatus = 2;
        brls::sync([]() {
            AddHostTab* instance = AddHostTab::activeInstance.load(std::memory_order_acquire);
            if (instance) {
                brls::View* spinnerRow = instance->getView("spinner_row");
                if (spinnerRow)
                    spinnerRow->setVisibility(brls::Visibility::GONE);
            }
        });
        return 0; }, 0x10000100, 0x10000, 0, 0, NULL);
    vita_log::info("[check_host] startVitaDiscovery: thread created");
    if (vitaDiscoveryThread >= 0)
    {
        vita_log::info("[check_host] startVitaDiscovery: thread created; starting...");
        int startResult = sceKernelStartThread(vitaDiscoveryThread, 0, nullptr);
        if (startResult < 0)
        {
            vita_log::error("[check_host] Failed to start mDNS thread (sceKernelStartThread): 0x%08X", startResult);
            vita_log::error("[check_host] Error start thread: 0x%08X\n", startResult);
            sceKernelDeleteThread(vitaDiscoveryThread);
            vitaDiscoveryThread = -1;
            udp_sniffer_vita_deinit();
            vitaDiscoveryStatus = 0;
            brls::sync([startResult]()
                {
                char msg[96];
                std::snprintf(msg, sizeof(msg), "[mDNS] Could not start discovery (0x%08X)", static_cast<unsigned int>(startResult));
                AddHostTab* instance = AddHostTab::activeInstance.load(std::memory_order_acquire);
                if (instance) {
                    brls::View* spinnerRow = instance->getView("spinner_row");
                    if (spinnerRow)
                        spinnerRow->setVisibility(brls::Visibility::GONE);
                    brls::Application::notify(msg);
                } else {
                    brls::Application::notify(msg);
                } });
        }
        else
        {
            vita_log::info("[check_host] mDNS thread running (sceKernelStartThread=0x%08X)", startResult);
        }
    }
    else
    {
        vita_log::error("[check_host] Failed to create Vita mDNS discovery thread");
        udp_sniffer_vita_deinit();
        vitaDiscoveryStatus.store(0, std::memory_order_release);
    }
}

bool isVitaDiscoveryActive()
{
    return vitaDiscoveryStatus.load(std::memory_order_acquire) == 1;
}
#endif

#if defined(_WIN32)
static std::thread winDiscoveryThread;
static std::atomic<bool> winThreadActive { false };

void stopWinDiscovery()
{
    winThreadActive = false;
    if (winDiscoveryThread.joinable())
        winDiscoveryThread.join();
    udp_sniffer_win_deinit();
}

void startWinDiscovery(void (*hostFoundCb)(int, const char*, const char*, const char*, int))
{
    stopWinDiscovery();
    winThreadActive = true;
    udp_sniffer_win_init();
    udp_sniffer_win_set_callback(hostFoundCb);
    winDiscoveryThread = std::thread([]()
        {
        int ticks = 0;
        int max_ticks = 100;
        while (winThreadActive && ticks < max_ticks) {
            udp_sniffer_win_poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ticks++;
        }
        udp_sniffer_win_deinit();
        winThreadActive = false;
        brls::sync([]() {
            AddHostTab* instance = AddHostTab::activeInstance.load(std::memory_order_acquire);
            if (instance) {
                brls::View* spinnerRow = instance->getView("spinner_row");
                if (spinnerRow)
                    spinnerRow->setVisibility(brls::Visibility::GONE);
            }
        }); });
}

bool isWinDiscoveryActive()
{
    return winThreadActive;
}
#endif

} // namespace check_host
