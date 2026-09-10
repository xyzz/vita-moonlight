/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <atomic>
#include <borealis.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <memory>
#include <string>
#include <vector>

class AddHostTab : public brls::Box
{
  public:
    AddHostTab();
    ~AddHostTab() override;
    static brls::View* create();
    void startDeviceDiscovery();
    void rebuildDiscoveredHostsUI();
    static std::atomic<AddHostTab*> activeInstance;
#ifdef _WIN32
    static void winHostFoundCb(int idx, const char* host, const char* pcname, const char* ip, int port);
#endif
    BRLS_BIND(brls::InputCell, ipField, "ip_field");
    BRLS_BIND(brls::InputCell, nameField, "name_field");
    BRLS_BIND(brls::SelectorCell, preferExternalSelector, "prefer_external_selector");
    BRLS_BIND(brls::Button, addButton, "add_button");
    BRLS_BIND(brls::Box, hostsList, "hosts_list");
    BRLS_BIND(brls::Box, loader, "loader");
    std::atomic<bool> pairingInProgress { false };
    std::atomic<bool> inputsUnblockedByPin { false };

    std::shared_ptr<std::atomic<bool>> aliveToken = std::make_shared<std::atomic<bool>>(true);
    std::vector<std::pair<std::string, std::string>> discoveredHosts;
};
