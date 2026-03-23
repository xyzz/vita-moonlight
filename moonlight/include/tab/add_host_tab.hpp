/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <atomic>
#include <borealis.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "utils/host_search.hpp"

// Secure context for asynchronous pairing
struct PairingContext
{
    std::atomic<bool> cancelled { false };
    // You can add more details here if necessary
};

class AddHostTab : public brls::Box, public std::enable_shared_from_this<AddHostTab>
{
  public:
    AddHostTab();
    virtual ~AddHostTab();
    static brls::View* create();
    void refreshHostsList();
    void startDeviceDiscovery();
#ifdef _WIN32
    static AddHostTab* winInstance;
    static void winHostFoundCb(int idx, const char* host, const char* pcname, const char* ip, int port);
#endif
#if defined(__PSV__)
    static AddHostTab* vitaInstance;
    void rebuildDiscoveredHostsUI();
#endif
    BRLS_BIND(brls::InputCell, ipField, "ip_field");
    BRLS_BIND(brls::InputCell, nameField, "name_field");
    BRLS_BIND(brls::SelectorCell, preferExternalSelector, "prefer_external_selector");
    BRLS_BIND(brls::Button, addButton, "add_button");
    BRLS_BIND(brls::Box, hostsList, "hosts_list");
    std::atomic<bool> discoveryRunning { false };
    std::thread discoveryThread;
    BRLS_BIND(brls::Box, loader, "loader");
    std::atomic<bool> pairingInProgress { false };
    // Check if the inputs were already unlocked when the PIN appeared
    std::atomic<bool> inputsUnblockedByPin { false };

    // --- Secure and asynchronous pairing ---
    std::shared_ptr<PairingContext> pairingContext;
    std::thread pairingThread;
#if defined(__PSV__)
    std::vector<std::pair<std::string, std::string>> discoveredHosts;
#endif
};
