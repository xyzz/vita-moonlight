/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <atomic>
#include <borealis.hpp>

#include "model/HostStorage.hpp" // For HostInfo
#include "utils/host_search.hpp"

class HostsTab : public brls::Box
{
  public:
    HostsTab();
    static brls::View* create();
    void refreshHostsList();
    // Requests that the main activity be completely reloaded (rebuilds the home)
    // Safe implementation: Execute the recreation of the MainActivity content in the UI thread.
    static void requestGlobalRefresh();
    BRLS_BIND(brls::Box, hostsList, "hosts_list");
    // Indicates that the HostsTab has already finished its construction and initialization
    // (maintained for possible future lifecycle checks)
    std::atomic<bool> initialized { false };
};
// check_host.hpp
#pragma once

#if defined(__PSV__)
#include <psp2/kernel/threadmgr.h>
#endif

namespace check_host
{
#if defined(__PSV__)
void startVitaDiscovery(void (*hostFoundCb)(int, const char*, const char*, const char*, int));
void stopVitaDiscovery();
bool isVitaDiscoveryActive();
#endif
#if defined(_WIN32)
void startWinDiscovery(void (*hostFoundCb)(int, const char*, const char*, const char*, int));
void stopWinDiscovery();
bool isWinDiscoveryActive();
#endif
} // namespace check_host
