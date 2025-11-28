#pragma once

#include <borealis.hpp>
#include <string>

class MicrophoneSettingsTab : public brls::Box
{
  public:
    MicrophoneSettingsTab();
    ~MicrophoneSettingsTab(); // Destructor to stop test on exit

    static brls::View* create();

  private:
    std::string getGainText(float gain);
    std::string currentHostAddress; // Selected host for remote testing
    int currentHostPort = 48100;

    BRLS_BIND(brls::Slider, gainSlider, "gainSlider");
    BRLS_BIND(brls::Label, gainLabel, "gainLabel");
    BRLS_BIND(brls::BooleanCell, opusToggle, "opusToggle");
    BRLS_BIND(brls::BooleanCell, testMicrophoneToggle, "testMicrophoneToggle");
    BRLS_BIND(brls::SelectorCell, hostSelector, "hostSelector");
    BRLS_BIND(brls::BooleanCell, testMicrophoneRemoteToggle, "testMicrophoneRemoteToggle");
    BRLS_BIND(brls::DetailCell, portInputCell, "portInputCell");
};
