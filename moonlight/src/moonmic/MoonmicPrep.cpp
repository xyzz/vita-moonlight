#include "MoonmicPrep.hpp"

#include <borealis.hpp>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

#include "GameStreamClient.hpp"
#include "moonmic/MoonmicBridge.hpp"

namespace moonmic
{
namespace
{

    // Forward declaration
    void waitForApps(const HostInfo host, const PrepCallbacks callbacks);

    void continueHandshake(bool userWantedSwitch,
        int hostW, int hostH,
        const HostInfo host,
        const std::string micHost, int micPort,
        const PrepCallbacks callbacks)
    {

        brls::async([=]()
            {
        auto& bridge = moonmic::MoonmicBridge::getInstance();

        if (userWantedSwitch) {
            // User chose to switch (Restart Host) -> Send FORCE flag
            brls::Logger::info("[MoonmicPrep] User chose switch -> Sending FORCE handshake");
            bridge.sendResolutionHandshake(micHost, micPort, true);
            // After force, host restarts. We proceed to wait for apps.
        } else {
            // User chose to keep host resolution -> Update local target
            brls::Logger::info("[MoonmicPrep] User chose keep host res -> Updating local target to {}x{}", hostW, hostH);
            bridge.setTargetResolution(hostW, hostH);
        }

        // Give some time for changes to apply or host to restart
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Proceed to wait for apps
        waitForApps(host, callbacks); });
    }

    void waitForApps(const HostInfo host, const PrepCallbacks callbacks)
    {
        // This runs in async thread already (called from continueHandshake or startHandshake)
        // But to be safe and consistent with recursive structure, we can ensure we are async logic.
        // Since we are moving logic here, just put the loop body.

        bool appsReady = false;
        for (int i = 0; i < 8; ++i)
        { // Increased retries for restart time
            bool connected = GameStreamClient::instance().connect(host);
            if (!connected)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                continue;
            }

            std::vector<RemoteAppInfo> apps;
            GameStreamClient::instance().getAppList(host.ip, [&apps](const std::vector<RemoteAppInfo>& a)
                { apps = a; });
            if (!apps.empty())
            {
                appsReady = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
        }

        brls::sync([appsReady, callbacks]()
            {
        if (callbacks.onDone)
            callbacks.onDone(appsReady); });
    }

    void startHandshake(const HostInfo hostCopy,
        const StreamConfiguration sc,
        const VideoSettings vs,
        const PrepCallbacks callbacks)
    {
        if (callbacks.onStart)
            callbacks.onStart();

        std::string micHost = vs.microphone_host_ip.empty() ? hostCopy.ip : vs.microphone_host_ip;
        int micPort         = vs.microphone_port > 0 ? vs.microphone_port : MOONMIC_DEFAULT_PORT;

        brls::async([hostCopy, sc, vs, micHost, micPort, callbacks]()
            {
    auto& bridge = moonmic::MoonmicBridge::getInstance();
    bridge.loadConfig();
    if (sc.width > 0 && sc.height > 0) {
      bridge.setTargetResolution(static_cast<uint16_t>(sc.width), static_cast<uint16_t>(sc.height));
    }

    bool success = false;
    // Single fast handshake attempt
    for (int i = 0; i < 1 && !success; ++i) {
        auto result = bridge.sendResolutionHandshake(micHost, micPort, false);
        success = result.success;

        if (success) {
            if (result.mismatch) {
                int hostW = result.current_width;
                int hostH = result.current_height;
                brls::Logger::warning("[MoonmicPrep] Resolution mismatch detected, Host has {}x{}", hostW, hostH);

                // Show dialog on main thread AND RETURN from this thread
                brls::sync([hostW, hostH, hostCopy, micHost, micPort, callbacks]() {
                    brls::Box* holder = new brls::Box(brls::Axis::COLUMN);
                    holder->setMargins(20, 20, 20, 20);

                    brls::Label* text = new brls::Label();
                    text->setText(fmt::format(
                        "Resolution Mismatch\n\n"
                        "Host: {}x{}\n\n"
                        "Switch host to match Vita?",
                        hostW, hostH
                    ));
                    text->setHorizontalAlign(brls::HorizontalAlign::CENTER);
                    text->setMarginBottom(18);
                    holder->addView(text);

                    brls::Box* buttons = new brls::Box(brls::Axis::ROW);
                    buttons->setJustifyContent(brls::JustifyContent::CENTER);

                    brls::Button* btnYes = new brls::Button();
                    btnYes->setText("Switch (Restart)");
                    btnYes->setMargins(0, 10, 0, 10);

                    brls::Button* btnNo = new brls::Button();
                    btnNo->setText("Keep Host Res");
                    btnNo->setMargins(0, 10, 0, 10);

                    buttons->addView(btnYes);
                    buttons->addView(btnNo);
                    holder->addView(buttons);

                    brls::Dialog* dialog = new brls::Dialog(holder);
                    dialog->setCancelable(false);

                    // Actions
                    btnYes->registerClickAction([dialog, hostW, hostH, hostCopy, micHost, micPort, callbacks](brls::View*) -> bool {
                        dialog->dismiss();
                        // Continue workflow: Switch
                        continueHandshake(true, hostW, hostH, hostCopy, micHost, micPort, callbacks);
                        return true;
                    });

                    btnNo->registerClickAction([dialog, hostW, hostH, hostCopy, micHost, micPort, callbacks](brls::View*) -> bool {
                        dialog->dismiss();
                        // Continue workflow: Keep
                        continueHandshake(false, hostW, hostH, hostCopy, micHost, micPort, callbacks);
                        return true;
                    });

                    dialog->open();
                });
                return; // EXIT ASYNC THREAD - Wait for user interaction
            }
        }
    }

    // If we are here, either success+no mismatch, or failed 1 time
    // In either case, we proceed to wait for apps (if success) or fail (if !success)

    if (success) {
        waitForApps(hostCopy, callbacks);
    } else {
        // Failed handshake completely
        brls::sync([callbacks]() {
            if (callbacks.onDone) callbacks.onDone(false); // Or true if we want to allow continue anyway?
            // Better to fail safely or try anyway? Original logic implied fail.
            // Let's try anyway if it was just handshake fail.
             if (callbacks.onDone) callbacks.onDone(true); // Let session check apps
        });
    } });
    }

} // namespace

void ensureSunshineReadyWithPrompt(const HostInfo& host,
    const StreamConfiguration& streamCfg,
    const VideoSettings& videoCfg,
    bool& resolutionPromptShown,
    const PrepCallbacks& callbacks)
{
    // Disable blind prompt. We now use interactive handshake check.
    bool shouldPrompt = false;
    /*
    bool shouldPrompt = (!resolutionPromptShown && streamCfg.width > 0 && streamCfg.height > 0 &&
                         !(streamCfg.width == 1280 && streamCfg.height == 720));
    */

    auto beginHandshake = [host, streamCfg, videoCfg, callbacks]()
    {
        startHandshake(host, streamCfg, videoCfg, callbacks);
    };

    if (shouldPrompt)
    {
        resolutionPromptShown = true;

        std::string msg = brls::getStr("moonlight/session/app_select/resolution_prompt_body");
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%ux%u", streamCfg.width, streamCfg.height);
            size_t pos = msg.find("$(res)");
            if (pos != std::string::npos)
                msg.replace(pos, 6, buf);
        }

        auto* holder = new brls::Box(brls::Axis::COLUMN);
        auto* label  = new brls::Label();
        label->setText(msg);
        label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        label->setMarginBottom(18);
        holder->addView(label);

        auto* btnRow = new brls::Box(brls::Axis::ROW);
        btnRow->setJustifyContent(brls::JustifyContent::CENTER);
        btnRow->setAlignItems(brls::AlignItems::CENTER);

        auto* btnContinue = new brls::Button();
        btnContinue->setText(brls::getStr("moonlight/session/app_select/resolution_prompt_continue"));
        btnContinue->setStyle(&brls::BUTTONSTYLE_HIGHLIGHT);
        btnContinue->setMargins(0, 8, 8, 0);

        auto* btnCancel = new brls::Button();
        btnCancel->setText(brls::getStr("moonlight/session/app_select/resolution_prompt_cancel"));
        btnCancel->setStyle(&brls::BUTTONSTYLE_PRIMARY);

        btnRow->addView(btnContinue);
        btnRow->addView(btnCancel);
        holder->addView(btnRow);
        holder->setPadding(18, 18, 18, 18);

        auto* dialog = new brls::Dialog(holder);
        dialog->setCancelable(true);

        btnContinue->registerClickAction([dialog, beginHandshake](brls::View*) -> bool
            {
      dialog->dismiss();
      beginHandshake();
      return true; });

        btnCancel->registerClickAction([dialog, callbacks](brls::View*) -> bool
            {
      dialog->dismiss();
      if (callbacks.onCancel)
        callbacks.onCancel();
      return true; });

        dialog->open();
        return;
    }

    resolutionPromptShown = true;
    beginHandshake();
}

} // namespace moonmic
