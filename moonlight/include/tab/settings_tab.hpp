
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

#include <borealis.hpp>

class SettingsTab : public brls::Box
{
  public:
    SettingsTab();

    BRLS_BIND(brls::RadioCell, radio, "radio");
    BRLS_BIND(brls::BooleanCell, boolean, "boolean");
    BRLS_BIND(brls::SelectorCell, selector, "selector");
    BRLS_BIND(brls::InputCell, input, "input");
    BRLS_BIND(brls::InputNumericCell, inputNumeric, "inputNumeric");
    BRLS_BIND(brls::DetailCell, ipAddress, "ipAddress");
    BRLS_BIND(brls::DetailCell, dnsServer, "dnsServer");
    BRLS_BIND(brls::BooleanCell, debug, "debug");
    BRLS_BIND(brls::BooleanCell, bottomBar, "bottomBar");
    BRLS_BIND(brls::BooleanCell, alwaysOnTop, "alwaysOnTop");
    BRLS_BIND(brls::BooleanCell, fps, "fps");
    BRLS_BIND(brls::SelectorCell, swapInterval, "swapInterval");
    BRLS_BIND(brls::SliderCell, slider, "slider");
    BRLS_BIND(brls::SliderCell, settingsOpacitySlider, "settingsOpacitySlider");
    BRLS_BIND(brls::BooleanCell, keepAwakeToggle, "keepAwakeToggle");
    BRLS_BIND(brls::DetailCell, notify, "notify");
    BRLS_BIND(brls::SelectorCell, languageSelector, "languageSelector");

    // New elements for streaming configuration
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
    // lowLatencyToggle eliminated
    BRLS_BIND(brls::BooleanCell, framePacerToggle, "framePacerToggle");
    BRLS_BIND(brls::BooleanCell, centerRegionToggle, "centerRegionToggle");
    BRLS_BIND(brls::BooleanCell, showFpsToggle, "showFpsToggle");
    BRLS_BIND(brls::BooleanCell, debugLogToggle, "debugLogToggle");
    BRLS_BIND(brls::BooleanCell, refFrameInvalidationToggle, "refFrameInvalidationToggle");
    BRLS_BIND(brls::DetailCell, gyroSettingsEntry, "gyroSettingsEntry");
    BRLS_BIND(brls::BooleanCell, doubleTapSprintToggle, "doubleTapSprintToggle");
    BRLS_BIND(brls::SelectorCell, touchscreenModeSelector, "touchscreenModeSelector");
    BRLS_BIND(brls::SelectorCell, gamepadTypeSelector, "gamepadTypeSelector");
    BRLS_BIND(brls::BooleanCell, swapShoulderButtonsToggle, "swapShoulderButtonsToggle");
    BRLS_BIND(brls::DetailCell, keyboardConfigureCell, "keyboardConfigureCell");
    BRLS_BIND(brls::DetailCell, shortcutsConfigureCell, "shortcutsConfigureCell");
    BRLS_BIND(brls::DetailCell, rearTouchSettingsEntry, "rearTouchSettingsEntry");
    BRLS_BIND(brls::DetailCell, frontTouchSettingsEntry, "frontTouchSettingsEntry");
    BRLS_BIND(brls::DetailCell, trackpadSettingsEntry, "trackpadSettingsEntry");

    // Microphone settings
    BRLS_BIND(brls::BooleanCell, microphoneToggle, "microphoneToggle");
    BRLS_BIND(brls::DetailCell, microphoneConfigureCell, "microphoneConfigureCell");

    static inline brls::SelectorCell* languageSelectorPtr = nullptr;

    static brls::View* create();

  private:
    std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);
    void initAsync();
};
