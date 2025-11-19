#pragma once

#include <atomic>
#include <borealis.hpp>
#include <memory>
#include <string>

#include "GameStreamClient.hpp" // RemoteAppInfo
#include "model/HostStorage.hpp" // HostInfo
#include "session/overlay/test_overlay_stream.hpp"
#include "session/overlay/vita_pause_overlay.hpp"
#include "session/overlay/vita_stream_overlay_view.hpp"

// Vista principal de la sesión de streaming (usando XML)
class SessionMainView : public brls::Box
{
  public:
    SessionMainView(const HostInfo& host, const RemoteAppInfo& app);
    ~SessionMainView();
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

  private:
    void openSessionMenu();
    bool isPauseOverlayOpen() const { return pauseOverlayOpen.load(); }
    HostInfo host;
    RemoteAppInfo app;
    std::unique_ptr<VitaStreamOverlayView> overlayStatsView;
    std::unique_ptr<TestOverlayStream> testOverlay;
    BRLS_BIND(brls::Label, title, "title");
    BRLS_BIND(brls::Label, appLabel, "appLabel");
    BRLS_BIND(brls::Label, info, "info");
    BRLS_BIND(brls::Button, endBtn, "endBtn");
    static inline std::atomic<bool> pauseOverlayOpen { false };
};

// Función para lanzar la pantalla principal de sesión
void showSessionMain(const HostInfo& host, const RemoteAppInfo& app);
