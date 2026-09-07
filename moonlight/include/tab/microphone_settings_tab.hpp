#pragma once

#include <atomic>
#include <borealis.hpp>
#include <chrono>
#include <memory>
#include <string>

class MicrophoneSettingsTab : public brls::Box
{
  public:
    MicrophoneSettingsTab();
    ~MicrophoneSettingsTab();

    static brls::View* create();

  private:
    std::string getGainText(float gain);
    void updateConnectionStatus();

    std::string currentHostAddress;
    int currentHostPort = 48100;

    BRLS_BIND(brls::Slider, gainSlider, "gainSlider");
    BRLS_BIND(brls::Label, gainLabel, "gainLabel");
    BRLS_BIND(brls::BooleanCell, opusToggle, "opusToggle");
    BRLS_BIND(brls::BooleanCell, testMicrophoneToggle, "testMicrophoneToggle");
    BRLS_BIND(brls::SelectorCell, hostSelector, "hostSelector");
    BRLS_BIND(brls::BooleanCell, testMicrophoneRemoteToggle, "testMicrophoneRemoteToggle");
    BRLS_BIND(brls::DetailCell, portInputCell, "portInputCell");
    BRLS_BIND(brls::Rectangle, connectionLED, "connectionLED");
    BRLS_BIND(brls::Label, hostStatusLabel, "hostStatusLabel");

    bool monitoringConnection = false;

    std::chrono::steady_clock::time_point lastConnectionCheck;
    static constexpr int CONNECTION_CHECK_INTERVAL_MS = 500;

    std::shared_ptr<std::atomic<bool>> aliveToken = std::make_shared<std::atomic<bool>>(true);
    void loadHostsAsync();

    void frame(brls::FrameContext* ctx) override
    {
        brls::Box::frame(ctx);

        if (monitoringConnection)
        {
            auto now     = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastConnectionCheck).count();
            if (elapsed >= CONNECTION_CHECK_INTERVAL_MS)
            {
                updateConnectionStatus();
                lastConnectionCheck = now;
            }
        }
    }
};
