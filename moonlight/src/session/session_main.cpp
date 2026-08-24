#include "session/session_main.hpp"

#include <borealis/extern/nanovg/nanovg.h>

#include <borealis/core/application.hpp>
#include <borealis/core/frame_context.hpp>
#include <borealis/core/style.hpp>
#include <memory>

#include "ConfigManager.hpp"
#include "borealis.hpp"
#include "controller/ControllerInput.hpp"
#include "controller/keyboard/keyboard_launcher.hpp"
#include "debug.hpp"
#include "session/overlay/ingame_overlay_view.hpp"
#include "session/overlay/vita_pause_overlay.hpp"
#include "session/vita_session.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/modules/vita_globals.hpp"

extern "C"
{
    int vita_dp_init(void);
    void vita_dp_fini(void);
    int vita_dp_present_frame(void);
    bool vita_dp_is_active(void);
    void vita_frame_pacer_start(int paceFps);
    void vita_frame_pacer_stop(int restoreFps);
}

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

    g_video_settings_snapshot = videoSettings;

    unsigned targetFps = (unsigned)streamCfg.fps;
    if (targetFps == 0)
        targetFps = 60;
    if (targetFps > 60)
        targetFps = 60;

    // FPS and swapInterval are already configured in main.cpp
    // No need to reconfigure here - main.cpp handles both UI and streaming
    brls::Application::setFPSStatus(true); // Enable FPS status for logging
    vita_log::info("[SessionMainView] Init render config (cfg_fps=%d -> swapInterval=%d)", targetFps, videoSettings.swap_interval);

    brls::Application::setLowLatencyMode(true);

    // Wake the render loop as soon as the decoder publishes a frame.
    if (videoSettings.swap_interval == 0)
    {
        vita_frame_pacer_start(61);
    }

    if (vita_dp_init() == 0)
    {
        prePresentSub        = brls::Application::getPrePresentEvent()->subscribe([]()
            {
            if (vita_dp_is_active()) {
                vita_dp_present_frame();
            } });
        prePresentSubscribed = true;

        // NanoVG flushes after the direct quad, so the session background must stay clear.
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    }

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

    if (!vita_dp_is_active())
    {
        if (vg)
        {
            VitaVideoRenderer::instance().drawNVG(vg, width, height, 1.0f);
        }
        else
        {
            VitaVideoRenderer::instance().draw(width, height);
        }
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

void showSessionMain(const HostInfo& host, const RemoteAppInfo& app)
{
    auto* view = new SessionMainView(host, app);
    brls::Application::pushActivity(new brls::Activity(view));
}

SessionMainView::~SessionMainView()
{
    if (prePresentSubscribed)
    {
        brls::Application::getPrePresentEvent()->unsubscribe(prePresentSub);
        prePresentSubscribed = false;
    }

    vita_frame_pacer_stop(61);
    brls::Application::setLowLatencyMode(false);
    vita_dp_fini();
    overlayStatsView.release();
    if (g_controllerInput)
    {
        delete g_controllerInput;
        g_controllerInput = nullptr;
    }
    g_controllerInput = new ControllerInputManager();
}
