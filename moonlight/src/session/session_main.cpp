/*
    session_main.cpp - Pantalla principal de sesión activa (streaming) para Moonlight PSVita/Windows
    Autor: aorsini + comunidad
*/
#include "session/session_main.hpp"

#include <borealis/extern/nanovg/nanovg.h>
#include <vita2d.h>

#include <borealis/core/application.hpp>
#include <borealis/core/frame_context.hpp>
#include <borealis/core/logger.hpp>
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

    // Ensure vita2d is ready BEFORE any potential draw (defensive)
    extern bool vita2d_inited;
    if (!vita2d_inited)
    {
        vita2d_init();
        vita2d_inited = true;
        vita2d_set_vblank_wait(0);
    }

    // Inicializar input manager
    g_controllerInput = new ControllerInputManager();

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
        brls::Application::giveFocus(overlay->getDefaultFocus()); }); // Reset input to avoid residual states of the previous UI
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
    brls::Application::setLimitedFPS(targetFps); // limit FPS according to configuration
    brls::Application::setSwapInterval(1); // enable vsync for stability
    brls::Logger::info("[SessionMainView] Init render config (cfg_fps={} -> limitedFPS={} swapInterval=1)", targetFps, targetFps);

    // Direct GXM mode eliminated. render_mode normalizes: 0=legacy,1=ffmpeg (future)
    bool settingsChanged = false;
    if (videoSettings.render_mode > 1)
    {
        videoSettings.render_mode = 0;
        brls::Logger::info("[SessionMainView] render_mode deprecated (>1) normalizado a 0 (legacy)");
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
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    if (g_controllerInput)
        g_controllerInput->handleInput();
    auto t1 = high_resolution_clock::now();

    bool isFfmpegMode = (g_video_settings_snapshot.render_mode == 1);
    if (isFfmpegMode)
    {
        // Fixed route: FFmpeg frames are rendered through NVG.
        if (vg)
        {
            VitaVideoRenderer::instance().drawNVG(vg, width, height, 1.0f);
        }
        else
        {
            // Fallback only if NVG context is unavailable.
            VitaVideoRenderer::instance().draw(width, height);
        }
    }
    else
    {
        // Fixed route: SCE/legacy decoder path uses vita2d.
        VitaVideoRenderer::instance().draw(width, height);
    }
    auto t2 = high_resolution_clock::now();

    Box::draw(vg, x, y, width, height, style, ctx);
    auto t3 = high_resolution_clock::now();

    // Throttled logging (once every ~500ms) to avoid spam
    static uint64_t lastFrameLogMs = 0;
    uint64_t now_ms                = duration_cast<milliseconds>(t3.time_since_epoch()).count();
    if (now_ms - lastFrameLogMs > 500)
    {
        lastFrameLogMs = now_ms;
        auto input_us  = duration_cast<microseconds>(t1 - t0).count();
        auto video_us  = duration_cast<microseconds>(t2 - t1).count();
        auto ui_us     = duration_cast<microseconds>(t3 - t2).count();
        auto total_us  = duration_cast<microseconds>(t3 - t0).count();
        int fps_i      = (int)std::lround(brls::Application::getFPS());
        // vita_debug_log("[SessionMain][PERF] fps=%d input=%lldus video=%lldus ui=%lldus total=%lldus", fps_i,
        //                (long long)input_us, (long long)video_us, (long long)ui_us, (long long)total_us);
    }
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
