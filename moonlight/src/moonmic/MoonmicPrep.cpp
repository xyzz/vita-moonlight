#include "MoonmicPrep.hpp"

#include <borealis.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "GameStreamClient.hpp"
#include "debug.hpp"
#include "moonmic/MoonmicBridge.hpp"

namespace moonmic
{
namespace
{

    void finish(const PrepCallbacks& callbacks, bool ready)
    {
        brls::sync([callbacks, ready]()
            {
            if (callbacks.onDone)
                callbacks.onDone(ready); });
    }

    void waitForHost(const HostInfo& host, const PrepCallbacks& callbacks)
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            if (GameStreamClient::instance().connect(host))
            {
                std::vector<RemoteAppInfo> apps;
                GameStreamClient::instance().getAppList(host.ip,
                    [&apps](const std::vector<RemoteAppInfo>& result)
                    { apps = result; });
                if (!apps.empty())
                {
                    finish(callbacks, true);
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
        }

        finish(callbacks, false);
    }

    void applyMismatchChoice(bool switchResolution, int hostWidth, int hostHeight,
        const HostInfo& host, const std::string& moonmicHost, int moonmicPort,
        const PrepCallbacks& callbacks)
    {
        brls::async([=]()
            {
            auto& bridge = MoonmicBridge::getInstance();
            if (!switchResolution)
            {
                bridge.setTargetResolution(hostWidth, hostHeight);
                finish(callbacks, true);
                return;
            }

            auto result = bridge.sendResolutionHandshake(moonmicHost, moonmicPort, true);
            if (!result.success)
            {
                finish(callbacks, false);
                return;
            }
            waitForHost(host, callbacks); });
    }

    void showResolutionMismatch(const MoonmicBridge::HandshakeResult& result,
        const HostInfo& host, const std::string& moonmicHost, int moonmicPort,
        int targetWidth, int targetHeight, const PrepCallbacks& callbacks)
    {
        brls::sync([=]()
            {
            std::string message = brls::getStr("moonlight/session/app_select/resolution_prompt_body");
            const auto marker   = message.find("$(res)");
            if (marker != std::string::npos)
                message.replace(marker, 6, fmt::format("{}x{}", targetWidth, targetHeight));

            auto* dialog = new brls::Dialog(message);
            dialog->setCancelable(false);
            dialog->addButton(brls::getStr("moonlight/session/app_select/resolution_prompt_continue"),
                [=]()
                {
                    applyMismatchChoice(true, result.current_width, result.current_height,
                        host, moonmicHost, moonmicPort, callbacks);
                });
            dialog->addButton(brls::getStr("moonlight/session/app_select/resolution_prompt_cancel"),
                [=]()
                {
                    applyMismatchChoice(false, result.current_width, result.current_height,
                        host, moonmicHost, moonmicPort, callbacks);
                });
            dialog->open(); });
    }

}

void prepareSunshine(const HostInfo& host, const StreamConfiguration& streamConfig,
    const VideoSettings& videoSettings, const PrepCallbacks& callbacks)
{
    if (callbacks.onStart)
        callbacks.onStart();

    const std::string moonmicHost = videoSettings.microphone_host_ip.empty()
        ? host.ip
        : videoSettings.microphone_host_ip;
    const int moonmicPort         = videoSettings.microphone_port > 0
                ? videoSettings.microphone_port
                : MOONMIC_DEFAULT_PORT;

    brls::async([=]()
        {
            auto& bridge = MoonmicBridge::getInstance();
            bridge.loadConfig();
            if (streamConfig.width > 0 && streamConfig.height > 0)
                bridge.setTargetResolution(streamConfig.width, streamConfig.height);

            const auto result = bridge.sendResolutionHandshake(moonmicHost, moonmicPort, false);
            if (!result.success)
            {
                finish(callbacks, false);
                return;
            }
            if (!result.mismatch)
            {
                finish(callbacks, true);
                return;
            }

            vita_log::warning("[MoonmicPrep] Host resolution is %dx%d; requested %dx%d",
                result.current_width, result.current_height, streamConfig.width, streamConfig.height);
            showResolutionMismatch(result, host, moonmicHost, moonmicPort,
                streamConfig.width, streamConfig.height, callbacks); });
}

}
