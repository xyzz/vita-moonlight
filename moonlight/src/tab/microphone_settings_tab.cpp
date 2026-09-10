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
    gainSlider->setProgress((currentGain - 1.0f) / 49.0f);

    gainLabel->setText(getGainText(currentGain));

    gainSlider->getProgressEvent()->subscribe([this](float progress)
        {
        float gain = 1.0f + (progress * 49.0f);
        gain = std::round(gain);
        if (gain < 1.0f) gain = 1.0f;
        if (gain > 50.0f) gain = 50.0f;

        ConfigManager cfg;
        cfg.updateVideoSettings([gain](VideoSettings& settings) { settings.microphone_gain = gain; });

        MicrophoneManager::getInstance().setGain(gain);
        MicrophoneTester::getInstance().setGain(gain);

        gainLabel->setText(getGainText(gain)); });

    opusToggle->init(
        "Use Opus Compression",
        videoSettings.enable_microphone_compression,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.updateVideoSettings([value](VideoSettings& settings)
                { settings.enable_microphone_compression = value; });
            brls::Application::notify(value ? brls::getStr("moonlight/microphone/notify_opus_enabled") : brls::getStr("moonlight/microphone/notify_opus_disabled"));
            return true;
        });

    testMicrophoneToggle->init(
        brls::getStr("moonlight/microphone/test_toggle"),
        false,
        [this](bool value)
        {
            auto& tester = MicrophoneTester::getInstance();

            if (value)
            {
                ConfigManager cfg;
                cfg.load();
                VideoSettings settings = cfg.getVideoSettings();
                tester.setGain(settings.microphone_gain);

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
                currentHostPort    = hosts[selected].microphone_port;
            }
        });

    testMicrophoneRemoteToggle->init(
        brls::getStr("moonlight/microphone/test_remote_toggle"),
        false,
        [this](bool value)
        {
            if (value)
            {
                auto& mgr = MicrophoneManager::getInstance();
                mgr.start(currentHostAddress, currentHostPort);
                brls::Application::notify(brls::getStr("moonlight/microphone/notify_remote_started"));

                monitoringConnection = true;

                return true;
            }
            else
            {
                auto& mgr = MicrophoneManager::getInstance();
                mgr.stop();

                monitoringConnection = false;

                connectionLED->setColor(nvgRGBA(255, 0, 0, 255));
                hostStatusLabel->setText(currentHostAddress + " (Disconnected)");

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
    aliveToken->store(false, std::memory_order_release);
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

void MicrophoneSettingsTab::updateConnectionStatus()
{
    auto& mgr      = MicrophoneManager::getInstance();
    bool connected = mgr.isConnected();

    if (connected)
    {
        connectionLED->setColor(nvgRGBA(0, 255, 0, 255));
        hostStatusLabel->setText(currentHostAddress + " (Connected)");
    }
    else
    {
        connectionLED->setColor(nvgRGBA(255, 0, 0, 255));
        hostStatusLabel->setText(currentHostAddress + " (Disconnected)");
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

void MicrophoneSettingsTab::loadHostsAsync()
{
    std::shared_ptr<std::atomic<bool>> token = this->aliveToken;

    std::thread([this, token]()
        {
        std::vector<HostInfo> hosts = HostStorage::loadHosts();

        std::sort(hosts.begin(), hosts.end(), [](const HostInfo& a, const HostInfo& b) {
            return a.name < b.name;
        });

        brls::sync([this, token, hosts]() {
            if (!token->load(std::memory_order_acquire)) return;

            std::vector<std::string> hostNames;

            if (hosts.empty()) {
                hostNames.push_back(brls::getStr("moonlight/microphone/no_hosts"));
                currentHostAddress = "";
                currentHostPort = 48100;
            } else {
                for (const auto& host : hosts) {
                    hostNames.push_back(host.name + " (" + host.ip + ")");
                }
                 if (!hosts.empty()) {
                    currentHostAddress = hosts[0].ip;
                    currentHostPort = hosts[0].microphone_port;
                }
            }

            hostSelector->init(
                brls::getStr("moonlight/microphone/host_selector_title"),
                hostNames,
                0,
                [this, hosts](int selected) {
                    if (!hosts.empty() && selected >= 0 && selected < (int)hosts.size()) {
                        currentHostAddress = hosts[selected].ip;
                        currentHostPort = hosts[selected].microphone_port;
                    }
                }
            );
        }); })
        .detach();
}
