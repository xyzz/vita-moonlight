/*
    session_main.cpp - Pantalla principal de sesión activa (streaming) para Moonlight PSVita/Windows
    Autor: aorsini + comunidad
*/
#include "session/session_main.hpp"

#include <borealis/extern/nanovg/nanovg.h>

#include <borealis/core/application.hpp>
#include <borealis/core/frame_context.hpp>
#include <borealis/core/style.hpp>
#include <borealis/views/dialog.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib> // getenv
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "borealis.hpp"
#include "controller/ControllerInput.hpp"
#include "controller/keyboard/keyboard_launcher.hpp"
#include "debug.hpp"
#include "model/HostStorage.hpp"
#include "session/hotkey_manager.hpp"
#include "session/overlay/ingame_overlay_view.hpp"
#include "session/overlay/test_overlay_stream.hpp"
#include "session/overlay/vita_pause_overlay.hpp"
#include "session/vita_session.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/modules/vita_globals.hpp"
#include "video/legacy/vita.hpp"

// Constructor implementation
SessionMainView::SessionMainView(const HostInfo& host, const RemoteAppInfo& app)
    : brls::Box()
    , host(host)
    , app(app)
{
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setBackgroundColor(nvgRGBA(0, 0, 0, 255));

    this->inflateFromXMLRes("xml/views/session_main.xml");

    // Inicializar input manager
    g_controllerInput = new ControllerInputManager();

    // Enable PS button capture for streaming (sends Guide/Special button to host)
    if (g_controllerInput)
    {
        g_controllerInput->setStreamingActive(true);
        g_controllerInput->lockPSButton();
    }

    // Notify server of gamepad type saved in config
    VitaSession::notifyGamepadType();

    // Register the pause shortcut with ControllerInputManager (START+L+R)
    // Avoid opening multiple overlays by holding down the combination.
    g_controllerInput->setPauseCallback([this]()
        {
        // Atomically check-and-set the pause flag to avoid duplicate overlays
        if (SessionMainView::pauseOverlayOpen.exchange(true)) return;
        // Disable input sending while the overlay is open to
        // prevent buttons from interacting with the stream.
        if (g_controllerInput) g_controllerInput->setInputEnabled(false);
        auto overlay = new VitaPauseOverlay([this]() {
            // reset the flag and reactivate the input when the overlay closes
            SessionMainView::pauseOverlayOpen.store(false);
            if (g_controllerInput) g_controllerInput->setInputEnabled(true);
            // pop the overlay activity (Application::popActivity will manage input tokens and focus)
            brls::Application::popActivity();
        }, this->host);
        auto* activity = new brls::Activity(overlay);
        brls::Application::pushActivity(activity);
        brls::Application::giveFocus(overlay->getDefaultFocus()); });

    g_controllerInput->setKeyboardShortcutCallback([]()
        {
        if (SessionMainView::pauseOverlayOpen.load()) {
            return;
        }
        open_configured_keyboard(); });

    // Reset input to avoid residual states of the previous UI
    if (g_controllerInput)
        g_controllerInput->dropInput();

    // Hide base UI to leave only video and overlay
    if (title)
        title->setVisibility(brls::Visibility::GONE);
    if (appLabel)
        appLabel->setVisibility(brls::Visibility::GONE);
    if (info)
        info->setVisibility(brls::Visibility::GONE);
    if (endBtn)
        endBtn->setVisibility(brls::Visibility::GONE);

    // Note: Pause shortcut management is done from ControllerInputManager.
    // Previously it was also registered in HotkeyManager but this caused the
    // overlay will be created twice in race conditions. It avoids the
    // duplication by registering the callback only in the input manager.


    // Try to force render loop at 60fps if the host negotiates >30.
    // Recover streaming and video settings.
    ConfigManager cfgMgr;
    cfgMgr.load();
    StreamConfiguration streamCfg = cfgMgr.getStreamConfig();
    VideoSettings videoSettings   = cfgMgr.getVideoSettings();

    g_controllerInput->setTouchscreenMode(videoSettings.touchscreen_mode);

    if (!overlayStatsView)
    {
        overlayStatsView = std::make_unique<VitaStreamOverlayView>();
    }
    if (overlayStatsView)
    {
        this->addView(overlayStatsView.get());
    }

    // Test Overlay: Off
    // if (!testOverlay) {
    //     testOverlay = std::make_unique<TestOverlayStream>();
    // }
    // if (testOverlay) {
    //     testOverlay->setVisibility(brls::Visibility::GONE); // Desactivado
    //     this->addView(testOverlay.get());
    //     // Give focus to the test overlay so it can receive navigation
    //     // brls::sync([this]() {
    //     //     if (testOverlay) brls::Application::giveFocus(testOverlay.get());
    //     // });
    // }

    g_video_settings_snapshot = videoSettings;

    unsigned targetFps = (unsigned)streamCfg.fps;
    if (targetFps == 0)
        targetFps = 60;
    if (targetFps > 60)
        targetFps = 60;

    // On PS Vita GXM, hardware swap buffers is non-blocking on the main thread (it runs asynchronously in a worker thread).
    // Therefore, we must ALWAYS enable the software rate limiter to prevent the main loop from running at uncontrolled
    // speeds (150+ FPS) and overwriting GXM buffers before they are scanned out, which causes severe screen tearing.
    brls::Application::setLimitedFPS(targetFps);
    brls::Application::setSwapInterval(videoSettings.swap_interval);
    brls::Application::setFPSStatus(true); // Enable FPS status for logging
    vita_log::info("[SessionMainView] Init render config (cfg_fps=%d -> swapInterval=%d)", targetFps, videoSettings.swap_interval);

    // Direct GXM mode eliminated. render_mode normalizes: 0=legacy,1=ffmpeg (future)
    bool settingsChanged = false;
    if (videoSettings.render_mode > 1)
    {
        videoSettings.render_mode = 0;
        vita_log::info("[SessionMainView] render_mode deprecated (>1) normalizado a 0 (legacy)");
        settingsChanged = true;
    }
    if (settingsChanged)
    {
        cfgMgr.setVideoSettings(videoSettings);
        cfgMgr.save();
    }

    brls::Application::giveFocus(this);
}
void SessionMainView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    if (g_controllerInput)
        g_controllerInput->handleInput();

    if (vg)
    {
        VitaVideoRenderer::instance().drawNVG(vg, width, height, 1.0f);
    }
    else
    {
        VitaVideoRenderer::instance().draw(width, height);
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

// Function to launch the main session screen
void showSessionMain(const HostInfo& host, const RemoteAppInfo& app)
{
    auto* view = new SessionMainView(host, app);
    brls::Application::pushActivity(new brls::Activity(view));
}

SessionMainView::~SessionMainView()
{
    overlayStatsView.release();
    testOverlay.release();
    if (g_controllerInput)
    {
        delete g_controllerInput;
        g_controllerInput = nullptr;
    }
    g_controllerInput = new ControllerInputManager();
}
