/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once

#if defined(__PSV__)
#include <psp2/kernel/threadmgr.h>
#endif
#include <functional>

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
