/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <string>
#include <vector>

#ifdef __PSV__
struct HostInfoVita
{
    char name[64];
    char ip[32];
    bool preferExternal;
};
std::vector<HostInfoVita> loadHostsVita();
#else
struct SearchHostInfo
{
    std::string name;
    std::string ip;
    bool preferExternal;
};
std::vector<SearchHostInfo> loadHosts();
#endif
