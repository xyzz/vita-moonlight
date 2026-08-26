#pragma once

#include <atomic>
#include <borealis.hpp>
#include <memory>
#include <string>

#include "GameStreamClient.hpp" // RemoteAppInfo
#include "model/HostStorage.hpp" // HostInfo
#include "session/overlay/vita_pause_overlay.hpp"
#include "session/overlay/vita_stream_overlay_view.hpp"

// Streaming session main view (using XML)
class SessionMainView : public brls::Box
{
  public:
    SessionMainView(const HostInfo& host, const RemoteAppInfo& app);
    ~SessionMainView();
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    static void setKeepAwakeWhileStreaming(bool enabled);

  private:
    void openSessionMenu();
    bool isPauseOverlayOpen() const { return pauseOverlayOpen.load(); }
    HostInfo host;
    RemoteAppInfo app;
    std::unique_ptr<VitaStreamOverlayView> overlayStatsView;
    brls::VoidEvent::Subscription prePresentSub;
    bool prePresentSubscribed = false;
    BRLS_BIND(brls::Label, title, "title");
    BRLS_BIND(brls::Label, appLabel, "appLabel");
    BRLS_BIND(brls::Label, info, "info");
    BRLS_BIND(brls::Button, endBtn, "endBtn");
    static inline std::atomic<bool> pauseOverlayOpen { false };
    static inline std::atomic<bool> sessionActive { false };
};

// Function to launch the main session screen
void showSessionMain(const HostInfo& host, const RemoteAppInfo& app);
