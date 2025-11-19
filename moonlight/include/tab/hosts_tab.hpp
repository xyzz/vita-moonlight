/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <atomic>
#include <borealis.hpp>

#include "model/HostStorage.hpp" // Para HostInfo
#include "utils/host_search.hpp"

class HostsTab : public brls::Box
{
  public:
    HostsTab();
    static brls::View* create();
    void refreshHostsList();
    // Solicita que se recargue completamente la actividad principal (reconstruye el home)
    // Implementación segura: ejecuta la recreación del contenido de MainActivity en el hilo UI.
    static void requestGlobalRefresh();
    BRLS_BIND(brls::Box, hostsList, "hosts_list");
    // Indica que el HostsTab ya terminó su construcción e inicialización
    // (se mantiene para posibles futuras comprobaciones de ciclo de vida)
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
