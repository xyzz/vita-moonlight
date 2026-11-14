#include "session/overlay/vita_pause_overlay.hpp"

#include <borealis.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/applet_frame.hpp>
#include <chrono>
#include <thread>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "activity/main_activity.hpp"
#include "controller/ControllerInput.hpp"
#include "controller/keyboard/keyboard.hpp"
#include "controller/keyboard/keyboard_launcher.hpp"
#include "debug.hpp"
#include "session/session_app_select.hpp"
#include "session/session_main.hpp"
#include "session/vita_session.hpp"
#include "settings.hpp"
#include "tab/hosts_tab.hpp"
#include "tab/settings_tab.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/vita.hpp"

namespace
{

bool dismissSessionAppSelectIfPresent()
{
    auto stack = brls::Application::getActivitiesStack();
    if (stack.empty())
        return false;

    auto* mainActivity = dynamic_cast<MainActivity*>(stack.back());
    if (!mainActivity)
        return false;

    brls::View* content = mainActivity->getContentView();
    auto* applet        = dynamic_cast<brls::AppletFrame*>(content);
    if (!applet)
        return false;

    brls::View* currentView = applet->getContentView();
    auto* appSelect         = dynamic_cast<SessionAppSelect*>(currentView);
    if (!appSelect)
        return false;

    vita_log::info("[VitaPauseOverlay] Dismissing SessionAppSelect to return to hosts list");
    appSelect->dismiss([]()
        { vita_log::info("[VitaPauseOverlay] SessionAppSelect dismissed"); });
    return true;
}

// Schedules the removal of the SessionMain and SessionAppSelect activities once the overlay is gone.
// Iterates the activity stack and pops session-related activities (overlay, then SessionMain).
void returnToMainMenuAsync(int retries = 8)
{
    brls::delay(30, [retries]() mutable
        {
        if (brls::Application::isInputBlocks()) {
            if (retries > 0) {
                returnToMainMenuAsync(retries - 1);
            } else {
                while (brls::Application::isInputBlocks()) {
                    brls::Application::unblockInputs();
                }
                HostsTab::requestGlobalRefresh();
            }
            return;
        }

        auto stack = brls::Application::getActivitiesStack();
        if (stack.empty()) {
            HostsTab::requestGlobalRefresh();
            return;
        }

        brls::Activity* top = stack.back();
        if (!top) {
            HostsTab::requestGlobalRefresh();
            return;
        }

        // 1) If the pause overlay is on top, pop it first.
        //    (The overlay is wrapped in an Activity; SessionMainView is a Box view.)
        brls::View* topContent = top->getContentView();
        auto* topOverlay = dynamic_cast<VitaPauseOverlay*>(topContent);
        if (!topOverlay) {
            // Also try unwrapping from an AppletFrame or similar wrapper
            auto* applet = dynamic_cast<brls::AppletFrame*>(topContent);
            if (applet) topContent = applet->getContentView();
            topOverlay = dynamic_cast<VitaPauseOverlay*>(topContent);
        }
        if (topOverlay) {
            vita_log::info("[VitaPauseOverlay] Popping overlay activity first");
            brls::Application::popActivity(brls::TransitionAnimation::NONE, []() {
                // After popping overlay, recurse to pop SessionMainView
                returnToMainMenuAsync(4);
            });
            return;
        }

        // The modern keyboard owns an activity above SessionMainView. It must be
        // removed before the session activity can be reached.
        auto* keyboardOverlay = dynamic_cast<KeyboardOverlay*>(topContent);
        if (keyboardOverlay) {
            vita_log::info("[VitaPauseOverlay] Popping keyboard activity");
            brls::Application::popActivity(brls::TransitionAnimation::NONE, []() {
                returnToMainMenuAsync(4);
            });
            return;
        }

        // 2) If SessionMainView is on top now, pop it.
        auto* sessionMain = dynamic_cast<SessionMainView*>(topContent);
        if (!sessionMain) {
            auto* applet = dynamic_cast<brls::AppletFrame*>(topContent);
            if (applet) topContent = applet->getContentView();
            sessionMain = dynamic_cast<SessionMainView*>(topContent);
        }
        if (sessionMain) {
            vita_log::info("[VitaPauseOverlay] Popping SessionMain activity");
            brls::Application::popActivity(brls::TransitionAnimation::NONE, []() {
                vita_log::info("[VitaPauseOverlay] SessionMain popped, dismissing SessionAppSelect");
                brls::delay(50, []() {
                    if (dismissSessionAppSelectIfPresent()) {
                        vita_log::info("[VitaPauseOverlay] SessionAppSelect dismissed successfully");
                        return; // Old MainActivity now shows HostsTab, no need to push a new one
                    }
                    vita_log::error("[VitaPauseOverlay] SessionAppSelect not found, falling back to refresh");
                    HostsTab::requestGlobalRefresh();
                });
            });
            return;
        }

        // 3) Fallback: neither overlay nor session main found on top.
        if (dismissSessionAppSelectIfPresent()) {
            vita_log::warning("[VitaPauseOverlay] Dismissed SessionAppSelect via fallback");
            return;
        }

        if (retries > 0) {
            returnToMainMenuAsync(retries - 1);
        } else {
            vita_log::info("[VitaPauseOverlay] Max retries reached, refreshing hosts");
            HostsTab::requestGlobalRefresh();
        } });
}

}

VitaPauseOverlay::VitaPauseOverlay(std::function<void()> onClose, const HostInfo& hostInfo)
    : BaseOverlay()
    , onClose(std::move(onClose))
    , host(hostInfo)
{

    setHeaderText(brls::getStr("moonlight/session/pause/title"));

    std::vector<std::string> labels = {
        brls::getStr("moonlight/session/pause/resume"),
        brls::getStr("moonlight/tabs/settings"),
        // Option to open keyboard overlay
        brls::getStr("moonlight/session/pause/keyboard"),
        brls::getStr("moonlight/session/pause/disconnect"),
        brls::getStr("moonlight/session/pause/close_app")
    };
    setButtons(labels);

    setActivateCallback([this](int index)
        {
        switch (index) {
            case 0: // Resume
                this->resume();
                break;
            case 1: // Settings
                {
                    brls::View* settingsView = SettingsTab::create();
                    moonlight::settings::pushView(settingsView, brls::getStr("moonlight/tabs/settings"));
                }
                break;
            case 2: // Keyboard
                {
                    if (this->onClose) {
                        try {
                            auto cb = std::move(this->onClose);
                            this->onClose = nullptr;
                            cb();
                        } catch(...) {}
                    }

                    open_configured_keyboard();
                }
                break;
            case 3: // Disconnect
                this->disconnect();
                break;
            case 4: // Close App
                this->closeApp();
                break;
        } });

    vita_log::info("[VitaPauseOverlay] opened for host=%s", host.ip.c_str());
    // Instrumentation: Record FPS and video status when opening overlay
    try
    {
        int fps_i      = (int)std::lround(brls::Application::getFPS());
        bool fpsStatus = brls::Application::getFPSStatus();
        VitaVideoStats vstats {};
        vitavideo_get_stats(&vstats);
        vita_log::debug("[VitaPauseOverlay][INST] onOpen FPS=%d fpsStatus=%d video_last_frame=%u presented=%u decoded=%u target=%u",
            fps_i, fpsStatus ? 1 : 0, vstats.last_frame_number, vstats.frames_presented, vstats.frames_decoded, vstats.target_fps);
    }
    catch (...)
    {
    }
}

void VitaPauseOverlay::resume()
{
    vita_log::info("[VitaPauseOverlay] resume pressed");
    try
    {
        int fps_i = (int)std::lround(brls::Application::getFPS());
        VitaVideoStats vstats {};
        vitavideo_get_stats(&vstats);
        vita_log::debug("[VitaPauseOverlay][INST] resume FPS=%d video_presented=%u decoded=%u target=%u", fps_i, vstats.frames_presented, vstats.frames_decoded, vstats.target_fps);
    }
    catch (...)
    {
    }
    using namespace std::chrono;
    auto tstart = high_resolution_clock::now();
    if (onClose)
    {
        auto cb        = std::move(onClose);
        onClose        = nullptr;
        auto tcb_start = high_resolution_clock::now();
        cb();
        auto tcb_end  = high_resolution_clock::now();
        auto cb_us    = duration_cast<microseconds>(tcb_end - tcb_start).count();
        auto total_us = duration_cast<microseconds>(tcb_end - tstart).count();
        vita_log::debug("[VitaPauseOverlay][PERF] resume cb=%lld us total=%lld us", (long long)cb_us, (long long)total_us);
    }
}

void VitaPauseOverlay::disconnect()
{
    vita_log::info("[VitaPauseOverlay] disconnect pressed");
    // Instrumentation: record FPS/state before starting stop sequence
    try
    {
        int fps_i = (int)std::lround(brls::Application::getFPS());
        VitaVideoStats vstats {};
        vitavideo_get_stats(&vstats);
        vita_log::debug("[VitaPauseOverlay][INST] disconnect start FPS=%d video_last_frame=%u presented=%u decoded=%u target=%u",
            fps_i, vstats.last_frame_number, vstats.frames_presented, vstats.frames_decoded, vstats.target_fps);
    }
    catch (...)
    {
    }
    // Run session destruction in background
    std::string addr   = this->host.ip;
    auto storedOnClose = std::move(onClose);
    onClose            = nullptr;
    brls::Application::notify(brls::getStr("moonlight/session/pause/notify_disconnected"));
    std::thread([addr, storedOnClose]() mutable
        {
        try {
            auto s = VitaSession::active();
            if (s) s->stop(false);
        } catch (...) {}
        try {
            brls::sync([]() { VitaVideoRenderer::instance().destroyImage(brls::Application::getNVGContext()); });
        } catch (...) {}
        GameStreamClient::instance().clearActiveStream(addr);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        brls::sync([storedOnClose]() mutable {
            if (storedOnClose) {
                try { storedOnClose(); } catch(...) {}
            }
            returnToMainMenuAsync();
        }); })
        .detach();
}

void VitaPauseOverlay::closeApp()
{
    vita_log::info("[VitaPauseOverlay] close app pressed");
    std::string addr   = this->host.ip;
    auto storedOnClose = std::move(onClose);
    onClose            = nullptr;
    brls::Application::notify(brls::getStr("moonlight/session/pause/notify_app_closed"));
    std::thread([addr, storedOnClose]() mutable
        {
        try {
            GameStreamClient::instance().quitApp(addr);
        } catch (...) {}
        try {
            auto s = VitaSession::active();
            if (s) s->stop(false);
        } catch (...) {}
        try {
            brls::sync([]() { VitaVideoRenderer::instance().destroyImage(brls::Application::getNVGContext()); });
        } catch (...) {}
        GameStreamClient::instance().clearActiveStream(addr);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        brls::sync([storedOnClose]() mutable {
            if (storedOnClose) {
                try { storedOnClose(); } catch(...) {}
            }
            returnToMainMenuAsync();
        }); })
        .detach();
}

VitaPauseOverlay::~VitaPauseOverlay()
{
    if (onClose)
    {
        try
        {
            auto cb = std::move(onClose);
            onClose = nullptr;
            cb();
        }
        catch (...)
        {
            vita_log::info("[VitaPauseOverlay] exception calling onClose in dtor");
        }
    }
}
