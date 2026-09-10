/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once

#include <atomic>
#include <memory>

#include "GameStreamClient.hpp"
#include "borealis.hpp"
#include "model/HostStorage.hpp"
#include "view/grid_view.hpp"
class SessionAppSelect : public brls::Box
{
  public:
    SessionAppSelect(const std::string& hostName);
    ~SessionAppSelect();

  private:
    void populateAppList();
    void AppSelected(const RemoteAppInfo& app, bool forceStart = false);
    // Helpers to show/hide the connection dialog and manage the GridView
    brls::Dialog* showConnectingDialog(const std::string& msg, brls::Visibility& outPrevGridVis);
    void restoreGridViewAndInputs(brls::Visibility prevGridVis);

    BRLS_BIND(brls::Label, app_select_title, "app_select_title");
    BRLS_BIND(brls::Label, app_select_subtitle, "app_select_subtitle");
    BRLS_BIND(brls::Box, app_grid_container, "app_grid_container");
    BRLS_BIND(brls::Box, grid_placeholder, "grid_placeholder");
    BRLS_BIND(brls::Label, app_select_empty, "app_select_empty");
    BRLS_BIND(brls::ProgressSpinner, loading_spinner, "loading_spinner");

    HostInfo host;
    GridView* gridView             = nullptr;
    brls::ProgressSpinner* spinner = nullptr;
    bool sunshineReady             = false;
    bool sunshineCheckInFlight     = false;
    std::shared_ptr<std::atomic<bool>> isAlive;
};
