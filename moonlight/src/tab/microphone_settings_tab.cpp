#include "tab/microphone_settings_tab.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "ConfigManager.hpp"
#include "audio/MicrophoneManager.hpp"
#include "audio/MicrophoneTester.hpp"
#include "model/HostStorage.hpp"

MicrophoneSettingsTab::MicrophoneSettingsTab()
{
    this->inflateFromXMLRes("xml/tabs/microphone_settings.xml");

    ConfigManager config;
    config.load();
    VideoSettings videoSettings = config.getVideoSettings();

    float currentGain = videoSettings.microphone_gain;
    if (currentGain < 1.0f)
        currentGain = 1.0f;
    if (currentGain > 50.0f)
        currentGain = 50.0f;
    gainSlider->setProgress((currentGain - 1.0f) / 49.0f); // Normalize 1-50 to 0-1

    gainLabel->setText(getGainText(currentGain));

    gainSlider->getProgressEvent()->subscribe([this](float progress)
        {
        float gain = 1.0f + (progress * 49.0f);  // 1-50 range
        gain = std::round(gain);  // Round to nearest integer
        if (gain < 1.0f) gain = 1.0f;
        if (gain > 50.0f) gain = 50.0f;

        ConfigManager cfg;
        cfg.load();
        VideoSettings settings = cfg.getVideoSettings();
        settings.microphone_gain = gain;
        cfg.setVideoSettings(settings);
        cfg.save();

        // Apply gain immediately (works even during transmission!)
        MicrophoneManager::getInstance().setGain(gain);

        gainLabel->setText(getGainText(gain)); });

    opusToggle->init(
        "Use Opus Compression",
        videoSettings.enable_microphone_compression,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.load();
            VideoSettings settings                 = cfg.getVideoSettings();
            settings.enable_microphone_compression = value;
            cfg.setVideoSettings(settings);
            cfg.save();
            brls::Application::notify(value ? brls::getStr("moonlight/microphone/notify_opus_enabled") : brls::getStr("moonlight/microphone/notify_opus_disabled"));
            return true;
        });

    testMicrophoneToggle->init(
        brls::getStr("moonlight/microphone/test_toggle"),
        false, // Always start disabled
        [](bool value)
        {
            auto& tester = MicrophoneTester::getInstance();

            if (value)
            {
                if (tester.start())
                {
                    brls::Application::notify(brls::getStr("moonlight/microphone/notify_local_started"));
                    return true;
                }
                else
                {
                    brls::Application::notify(brls::getStr("moonlight/microphone/notify_local_failed"));
                    return false;
                }
            }
            else
            {
                tester.stop();
                brls::Application::notify(brls::getStr("moonlight/microphone/notify_local_stopped"));
                return true;
            }
        });

    std::vector<HostInfo> hosts = HostStorage::loadHosts();
    std::vector<std::string> hostNames;

    std::sort(hosts.begin(), hosts.end(), [](const HostInfo& a, const HostInfo& b)
        { return a.name < b.name; });

    if (hosts.empty())
    {
        hostNames.push_back(brls::getStr("moonlight/microphone/no_hosts"));
        currentHostAddress = "";
        currentHostPort    = 48100;
    }
    else
    {
        for (const auto& host : hosts)
        {
            hostNames.push_back(host.name + " (" + host.ip + ")");
        }
        currentHostAddress = hosts[0].ip;
        currentHostPort    = hosts[0].microphone_port;
    }

    hostSelector->init(
        brls::getStr("moonlight/microphone/host_selector_title"),
        hostNames,
        0,
        [this, hosts, videoSettings](int selected)
        {
            if (!hosts.empty() && selected >= 0 && selected < (int)hosts.size())
            {
                currentHostAddress = hosts[selected].ip;
                currentHostPort    = hosts[selected].microphone_port; // Use port from device.ini
            }
        });

    testMicrophoneRemoteToggle->init(
        brls::getStr("moonlight/microphone/test_remote_toggle"),
        false, // Always start disabled
        [this](bool value)
        {
            auto& mgr = MicrophoneManager::getInstance();

            if (value)
            {
                // Check if host is selected
                if (currentHostAddress.empty())
                {
                    brls::Application::notify(brls::getStr("moonlight/microphone/notify_no_host"));
                    return false; // Revert toggle
                }

                // Start remote test with selected host
                if (mgr.start(currentHostAddress, currentHostPort))
                {
                    brls::Application::notify(brls::getStr("moonlight/microphone/notify_remote_started"));
                    return true;
                }
                else
                {
                    brls::Application::notify(brls::getStr("moonlight/microphone/notify_remote_connecting"));
                    return true; // Keep toggle ON, auto-retry is enabled
                }
            }
            else
            {
                mgr.stop();
                brls::Application::notify(brls::getStr("moonlight/microphone/notify_remote_stopped"));
                return true;
            }
        });

    portInputCell->setDetailText(std::to_string(videoSettings.microphone_port));
    portInputCell->registerClickAction([this](brls::View* view)
        {
        brls::Application::notify("Port: " + std::to_string(currentHostPort) + " (editable in future update)");
        return true; });

    this->registerAction("Back", brls::BUTTON_B, [](brls::View*)
        {
        brls::Application::popActivity();
        return true; });
}

MicrophoneSettingsTab::~MicrophoneSettingsTab()
{
    auto& tester = MicrophoneTester::getInstance();
    if (tester.isRunning())
    {
        tester.stop();
    }

    auto& mgr = MicrophoneManager::getInstance();
    if (mgr.isRunning() || mgr.isRetrying())
    {
        mgr.stop();
    }
}

std::string MicrophoneSettingsTab::getGainText(float gain)
{
    int gainInt = static_cast<int>(std::round(gain));
    return "Gain: " + std::to_string(gainInt) + "x";
}

brls::View* MicrophoneSettingsTab::create()
{
    return new MicrophoneSettingsTab();
}
