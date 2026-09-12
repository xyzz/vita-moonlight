
/*
    Copyright 2021 natinusala
    Edit for AorsiniYT 2025

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include <atomic>
#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <vector>

class SettingsTab : public brls::Box
{
  public:
    SettingsTab();
    ~SettingsTab() override;

    BRLS_BIND(brls::DetailCell, ipAddress, "ipAddress");
    BRLS_BIND(brls::SelectorCell, swapInterval, "swapInterval");
    BRLS_BIND(brls::SliderCell, slider, "slider");
    BRLS_BIND(brls::SliderCell, settingsOpacitySlider, "settingsOpacitySlider");
    BRLS_BIND(brls::BooleanCell, keepAwakeToggle, "keepAwakeToggle");
    BRLS_BIND(brls::SelectorCell, languageSelector, "languageSelector");

    BRLS_BIND(brls::SelectorCell, renderModeSelector, "renderModeSelector");
    BRLS_BIND(brls::SelectorCell, streamResolutionSelector, "streamResolutionSelector");
    BRLS_BIND(brls::SelectorCell, resolutionSelector, "resolutionSelector");
    BRLS_BIND(brls::SelectorCell, fpsSelector, "fpsSelector");
    BRLS_BIND(brls::SelectorCell, bitrateSelector, "bitrateSelector");
    BRLS_BIND(brls::SelectorCell, pixelFormatSelector, "pixelFormatSelector");
    BRLS_BIND(brls::BooleanCell, sopsToggle, "sopsToggle");
    BRLS_BIND(brls::BooleanCell, networkOptimizationsToggle, "networkOptimizationsToggle");
    BRLS_BIND(brls::BooleanCell, localAudioToggle, "localAudioToggle");
    BRLS_BIND(brls::BooleanCell, fullscreenToggle, "fullscreenToggle");
    BRLS_BIND(brls::BooleanCell, framePacerToggle, "framePacerToggle");
    BRLS_BIND(brls::BooleanCell, centerRegionToggle, "centerRegionToggle");
    BRLS_BIND(brls::BooleanCell, showFpsToggle, "showFpsToggle");
    BRLS_BIND(brls::BooleanCell, debugLogToggle, "debugLogToggle");
    BRLS_BIND(brls::DetailCell, gyroSettingsEntry, "gyroSettingsEntry");
    BRLS_BIND(brls::BooleanCell, doubleTapSprintToggle, "doubleTapSprintToggle");
    BRLS_BIND(brls::SelectorCell, touchscreenModeSelector, "touchscreenModeSelector");
    BRLS_BIND(brls::SelectorCell, gamepadTypeSelector, "gamepadTypeSelector");
    BRLS_BIND(brls::BooleanCell, psButtonCaptureToggle, "psButtonCaptureToggle");
    BRLS_BIND(brls::BooleanCell, swapShoulderButtonsToggle, "swapShoulderButtonsToggle");
    BRLS_BIND(brls::DetailCell, keyboardConfigureCell, "keyboardConfigureCell");
    BRLS_BIND(brls::DetailCell, shortcutsConfigureCell, "shortcutsConfigureCell");
    BRLS_BIND(brls::DetailCell, rearTouchSettingsEntry, "rearTouchSettingsEntry");
    BRLS_BIND(brls::DetailCell, frontTouchSettingsEntry, "frontTouchSettingsEntry");
    BRLS_BIND(brls::DetailCell, trackpadSettingsEntry, "trackpadSettingsEntry");

    BRLS_BIND(brls::BooleanCell, microphoneToggle, "microphoneToggle");
    BRLS_BIND(brls::DetailCell, microphoneConfigureCell, "microphoneConfigureCell");

    static brls::View* create();

  private:
    struct AsyncState
    {
        std::atomic<bool> alive { true };
        std::atomic<std::uint64_t> opacityGeneration { 0 };
        std::atomic<float> pendingOpacity { 1.0f };
        std::atomic<bool> opacityDirty { false };
    };

    std::shared_ptr<AsyncState> asyncState = std::make_shared<AsyncState>();
    std::vector<int> displayModeValues;

    void initAsync();
    int prepareDisplayModeOptions(int renderMode, int displayMode, std::vector<std::string>& labels);
    void updateDisplayModeOptions(int renderMode, int displayMode);
};
