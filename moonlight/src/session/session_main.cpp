#include "session/session_main.hpp"

#include <borealis/extern/nanovg/nanovg.h>

#include <borealis/core/application.hpp>
#include <borealis/core/frame_context.hpp>
#include <borealis/core/style.hpp>
#include <memory>
#include <utility>

#include "ConfigManager.hpp"
#include "borealis.hpp"
#include "controller/ControllerInput.hpp"
#include "controller/keyboard/keyboard_launcher.hpp"
#include "controller/keyboard/keyboard_touch.hpp"
#include "controller/keyboard/legacy_keyboard.hpp"
#include "debug.hpp"
#include "session/overlay/vita_pause_overlay.hpp"
#include "session/vita_session.hpp"
#include "settings.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/modules/vita_globals.hpp"

extern "C"
{
    int vita_dp_init(void);
    void vita_dp_fini(void);
    int vita_dp_present_frame(void);
    bool vita_dp_is_active(void);
}

SessionMainView::SessionMainView(const HostInfo& host, const RemoteAppInfo& app, std::shared_ptr<VitaSession> session)
    : brls::Box()
    , host(host)
    , app(app)
    , session(std::move(session))
{
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setBackgroundColor(nvgRGBA(0, 0, 0, 255));

    this->inflateFromXMLRes("xml/views/session_main.xml");

    g_controllerInput = new ControllerInputManager();
    this->addGestureRecognizer(new KeyboardTouchRecognizer([](brls::GestureState state, const brls::Point& position)
        {
        auto* keyboard = g_controllerInput
            ? dynamic_cast<LegacyKeyboard*>(g_controllerInput->getActiveKeyboard())
            : nullptr;
        if (state == brls::GestureState::START) {
            return keyboard && keyboard->beginFocusButtonTouch(position.x, position.y);
        }
        if (keyboard && (state == brls::GestureState::END || state == brls::GestureState::INTERRUPTED)) {
            keyboard->endFocusButtonTouch();
        }
        return true; }));

    // Enable PS button capture for streaming (sends Guide/Special button to host)
    if (g_controllerInput)
    {
        g_controllerInput->setStreamingActive(true);
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

    g_controllerInput->setFrontTouchShortcutCallback([]()
        {
        if (!g_controllerInput) {
            return;
        }
        const bool enabled = g_controllerInput->toggleFrontTouchEnabled();
        brls::Application::notify(brls::getStr(enabled
                ? "moonlight/front_touch/notify_enabled"
                : "moonlight/front_touch/notify_disabled")); });

    // Reset input to avoid residual states of the previous UI
    if (g_controllerInput)
        g_controllerInput->dropInput();

    if (title)
        title->setVisibility(brls::Visibility::GONE);
    if (appLabel)
        appLabel->setVisibility(brls::Visibility::GONE);
    if (info)
        info->setVisibility(brls::Visibility::GONE);
    if (endBtn)
        endBtn->setVisibility(brls::Visibility::GONE);

    // ControllerInputManager owns the pause shortcut to avoid duplicate overlays.

    // Recover streaming and video settings.
    ConfigManager cfgMgr;
    cfgMgr.load();
    StreamConfiguration streamCfg = cfgMgr.getStreamConfig();
    VideoSettings videoSettings   = cfgMgr.getVideoSettings();

    sessionActive.store(true);
    setKeepAwakeWhileStreaming(videoSettings.keep_awake_while_streaming);

    g_controllerInput->setTouchscreenMode(videoSettings.touchscreen_mode);

    if (!overlayStatsView)
    {
        overlayStatsView = new VitaStreamOverlayView();
    }
    if (overlayStatsView)
    {
        this->addView(overlayStatsView);
    }

    unsigned targetFps = (unsigned)streamCfg.fps;
    if (targetFps == 0)
        targetFps = 60;
    if (targetFps > 60)
        targetFps = 60;

    brls::Application::setFPSStatus(true);
    const auto streamDisplayMode = moonlight::settings::normalizeStreamDisplayMode(
        videoSettings.swap_interval, videoSettings.render_mode);
    moonlight::settings::applyStreamDisplayMode(
        static_cast<int>(streamDisplayMode), videoSettings.render_mode,
        videoSettings.enable_frame_pacer, static_cast<int>(targetFps));
    const char* displayMode = "immediate";
    if (streamDisplayMode == moonlight::StreamDisplayMode::VBlank)
        displayMode = "vblank";
    else if (streamDisplayMode == moonlight::StreamDisplayMode::NextFrame)
        displayMode = "next-frame";
    vita_log::info("[SessionMainView] Display mode=%s target=%u fps pacer=%d",
        displayMode, targetFps, videoSettings.enable_frame_pacer);

    brls::Application::setLowLatencyMode(true);

    const bool directPresenterReady = vita_dp_init() == 0;
    prePresentSub                   = brls::Application::getPrePresentEvent()->subscribe([]()
        {
        const bool legacyDialogActive = is_legacy_dialog_keyboard_active();
        brls::Application::setCommonDialogVSync(legacyDialogActive);

        if (vita_dp_is_active()) {
            vita_dp_present_frame();
        } else if (legacyDialogActive) {
            VitaVideoRenderer::instance().drawNVG(
                brls::Application::getNVGContext(),
                static_cast<float>(brls::Application::windowWidth),
                static_cast<float>(brls::Application::windowHeight));
        } });
    prePresentSubscribed            = true;

    if (directPresenterReady)
    {
        // NanoVG flushes after the direct quad, so the session background must stay clear.
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    }

    brls::Application::giveFocus(this);
}

void SessionMainView::setKeepAwakeWhileStreaming(bool enabled)
{
    if (sessionActive.load())
    {
        brls::Application::getPlatform()->disableScreenDimming(enabled, "streaming", "moonlight");
    }
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

    if (g_controllerInput)
    {
        auto* legacyKeyboard = dynamic_cast<LegacyKeyboard*>(g_controllerInput->getActiveKeyboard());
        if (legacyKeyboard)
        {
            legacyKeyboard->drawOverlay(vg);
        }
    }
}

SessionMainView::~SessionMainView()
{
    sessionActive.store(false);
    brls::Application::getPlatform()->disableScreenDimming(false, "streaming", "moonlight");

    if (prePresentSubscribed)
    {
        brls::Application::getPrePresentEvent()->unsubscribe(prePresentSub);
        prePresentSubscribed = false;
    }

    brls::Application::setCommonDialogVSync(false);
    moonlight::settings::applyMenuDisplayMode();
    brls::Application::setLowLatencyMode(false);
    vita_dp_fini();
    if (session)
        session->stop(false);
    if (g_controllerInput)
    {
        delete g_controllerInput;
        g_controllerInput = nullptr;
    }
    g_controllerInput = new ControllerInputManager();
    session.reset();
}
