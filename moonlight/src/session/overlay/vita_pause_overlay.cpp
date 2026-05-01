#include "session/overlay/vita_pause_overlay.hpp"

#include <borealis.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/applet_frame.hpp>
#include <chrono>
#include <thread>

#include "GameStreamClient.hpp"
#include "activity/main_activity.hpp"
#include "controller/ControllerInput.hpp"
#include "controller/keyboard/keyboard_launcher.hpp"
#include "debug.hpp"
#include "session/session_app_select.hpp"
#include "session/session_main.hpp"
#include "session/vita_session.hpp"
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

    vita_debug_log("[VitaPauseOverlay] Dismissing SessionAppSelect to return to hosts list");
    appSelect->dismiss([]()
        { vita_debug_log("[VitaPauseOverlay] SessionAppSelect dismissed"); });
    return true;
}

// Schedules the removal of the SessionMain and SessionAppSelect activities once the overlay is gone.
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
        if (!stack.empty()) {
            brls::Activity* top = stack.back();
            if (top && dynamic_cast<SessionMainView*>(top->getContentView())) {
                // Pop SessionMain first
                brls::Application::popActivity(brls::TransitionAnimation::NONE, []() {
                    vita_debug_log("[VitaPauseOverlay] SessionMain popped, dismissing SessionAppSelect");
                    // SessionAppSelect is not an activity, it's a view inside MainActivity
                    // We need to dismiss it programmatically with a small delay
                    brls::delay(50, []() {
                        if (dismissSessionAppSelectIfPresent()) {
                            vita_debug_log("[VitaPauseOverlay] SessionAppSelect dismissed successfully");
                            HostsTab::requestGlobalRefresh();
                        } else {
                            vita_debug_log("[VitaPauseOverlay] SessionAppSelect not found, refreshing hosts anyway");
                            HostsTab::requestGlobalRefresh();
                        }
                    });
                });
                return;
            }
        }

        // Fallback: try to dismiss SessionAppSelect if we didn't find SessionMain
        if (dismissSessionAppSelectIfPresent()) {
            vita_debug_log("[VitaPauseOverlay] Dismissed SessionAppSelect via fallback");
            return;
        }

        if (retries > 0) {
            returnToMainMenuAsync(retries - 1);
        } else {
            vita_debug_log("[VitaPauseOverlay] Max retries reached, refreshing hosts");
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
                    auto* frame = new brls::AppletFrame(settingsView);
                    frame->setTitle(brls::getStr("moonlight/tabs/settings"));
                    auto* activity = new brls::Activity(frame);
                    brls::Application::pushActivity(activity, brls::TransitionAnimation::FADE);
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

    vita_debug_log("[VitaPauseOverlay] opened for host=%s", host.ip.c_str());
    // Instrumentation: Record FPS and video status when opening overlay
    try
    {
        int fps_i      = (int)std::lround(brls::Application::getFPS());
        bool fpsStatus = brls::Application::getFPSStatus();
        VitaVideoStats vstats {};
        vitavideo_get_stats(&vstats);
        vita_debug_log("[VitaPauseOverlay][INST] onOpen FPS=%d fpsStatus=%d video_last_frame=%u presented=%u decoded=%u target=%u",
            fps_i, fpsStatus ? 1 : 0, vstats.last_frame_number, vstats.frames_presented, vstats.frames_decoded, vstats.target_fps);
    }
    catch (...)
    {
    }
}

void VitaPauseOverlay::resume()
{
    vita_debug_log("[VitaPauseOverlay] resume pressed");
    try
    {
        int fps_i = (int)std::lround(brls::Application::getFPS());
        VitaVideoStats vstats {};
        vitavideo_get_stats(&vstats);
        vita_debug_log("[VitaPauseOverlay][INST] resume FPS=%d video_presented=%u decoded=%u target=%u", fps_i, vstats.frames_presented, vstats.frames_decoded, vstats.target_fps);
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
        vita_debug_log("[VitaPauseOverlay][PERF] resume cb=%lld us total=%lld us", (long long)cb_us, (long long)total_us);
    }
}

void VitaPauseOverlay::disconnect()
{
    vita_debug_log("[VitaPauseOverlay] disconnect pressed");
    // Instrumentation: record FPS/state before starting stop sequence
    try
    {
        int fps_i = (int)std::lround(brls::Application::getFPS());
        VitaVideoStats vstats {};
        vitavideo_get_stats(&vstats);
        vita_debug_log("[VitaPauseOverlay][INST] disconnect start FPS=%d video_last_frame=%u presented=%u decoded=%u target=%u",
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
            VitaSession* s = VitaSession::active();
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
    vita_debug_log("[VitaPauseOverlay] close app pressed");
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
            VitaSession* s = VitaSession::active();
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
            vita_debug_log("[VitaPauseOverlay] exception calling onClose in dtor");
        }
    }
}
