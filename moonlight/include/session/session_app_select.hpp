/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once

#include "GameStreamClient.hpp" // Para RemoteAppInfo
#include "borealis.hpp"
#include "model/HostStorage.hpp"
#include "view/grid_view.hpp"
class SessionAppSelect : public brls::Box
{
  public:
    SessionAppSelect(const std::string& hostName);
    ~SessionAppSelect();

    void onLayout() override;

  private:
    void populateAppList();
    void AppSelected(const RemoteAppInfo& app, bool forceStart = false);
    // Helpers para mostrar/ocultar el diálogo de conexión y gestionar el GridView
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
    // Evita remostrar el diálogo de sesión activa si el usuario ya lo gestionó
    bool suppressActiveDialog = false;
    // Evita mostrar más de una vez el diálogo de sesión activa dentro de la misma instancia
    bool activeDialogShown = false;
    // Controla espera previa a cargar apps hasta que Moonmic/Sunshine respondan
    bool sunshineReady         = false;
    bool sunshineCheckInFlight = false;
    bool moonmicNotified       = false;
    bool moonmicLastStatus     = false;
    // Prompt de resolución (se muestra al entrar al host, no al lanzar app)
    bool resolutionPromptShown = false;
};
