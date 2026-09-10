/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <borealis.hpp>

class HostsTab : public brls::Box
{
  public:
    HostsTab();
    static brls::View* create();
    void refreshHostsList();
    static void requestGlobalRefresh();
    BRLS_BIND(brls::Box, hostsList, "hosts_list");
};
