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

#include "tab/settings_tab.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "ConfigManager.hpp"
#include "controller/ControllerInput.hpp"
#include "session/session_main.hpp"
#include "session/stream_config.hpp"
#include "session/vita_session.hpp"
#include "tab/front_touch_settings_tab.hpp"
#include "tab/gyro_settings_tab.hpp"
#include "tab/keyboard_settings_tab.hpp"
#include "tab/microphone_settings_tab.hpp"
#include "tab/rear_touch_settings_tab.hpp"
#include "tab/shortcuts_settings_tab.hpp"
#include "tab/trackpad_settings_tab.hpp"
#ifndef _WIN32
#include <sys/stat.h>
#endif
#if defined(__PSV__)
#include <psp2/appmgr.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif
#include "Settings.hpp"
#include "debug.hpp"
#include "network/NetworkOptimizations.hpp"
#include "video/runtime_settings.hpp"

static void saveSettingsOpacity(float value)
{
    ConfigManager config;
    config.updateVideoSettings([value](VideoSettings& settings)
        { settings.settings_background_opacity = value; });
}

int SettingsTab::prepareDisplayModeOptions(int renderMode, int displayMode, std::vector<std::string>& labels)
{
    const auto& modes = moonlight::settings::streamDisplayModes(renderMode);
    labels.clear();
    labels.reserve(modes.size());
    displayModeValues.clear();
    displayModeValues.reserve(modes.size());

    int selected = 0;
    for (std::size_t i = 0; i < modes.size(); ++i)
    {
        const int value = static_cast<int>(modes[i]);
        displayModeValues.push_back(value);
        labels.push_back(brls::getStr(
            "moonlight/settings_tab/swap_interval/options/" + std::to_string(value)));
        if (value == displayMode)
            selected = static_cast<int>(i);
    }

    return selected;
}

void SettingsTab::updateDisplayModeOptions(int renderMode, int displayMode)
{
    std::vector<std::string> labels;
    const int selected = prepareDisplayModeOptions(renderMode, displayMode, labels);

    swapInterval->setData(std::move(labels));
    swapInterval->setSelection(selected, true);
}

SettingsTab::SettingsTab()
{
    this->inflateFromXMLRes("xml/tabs/settings.xml");

    ConfigManager config;
    config.load();
    StreamConfiguration streamConfig = config.getStreamConfig();
    VideoSettings videoSettings      = config.getVideoSettings();

    moonlight::settings::applyBackground(this, videoSettings.settings_background_opacity);

    settingsOpacitySlider->init(
        brls::getStr("moonlight/settings_tab/settings_background_opacity"),
        videoSettings.settings_background_opacity,
        [this](float value)
        {
            value = std::max(0.0f, std::min(1.0f, value));
            moonlight::settings::applyBackground(this, value);
            settingsOpacitySlider->setDetailText(fmt::format("{:.0f}%", value * 100.0f));

            std::shared_ptr<AsyncState> state = asyncState;
            state->pendingOpacity.store(value, std::memory_order_release);
            state->opacityDirty.store(true, std::memory_order_release);
            const std::uint64_t generation = state->opacityGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            brls::delay(250, [state, generation]()
                {
                if (!state->alive.load(std::memory_order_acquire) ||
                    state->opacityGeneration.load(std::memory_order_acquire) != generation ||
                    !state->opacityDirty.exchange(false, std::memory_order_acq_rel)) {
                    return;
                }
                saveSettingsOpacity(state->pendingOpacity.load(std::memory_order_acquire)); });
        });
    settingsOpacitySlider->setDetailText(fmt::format("{:.0f}%", videoSettings.settings_background_opacity * 100.0f));

    keepAwakeToggle->init(
        brls::getStr("moonlight/settings_tab/keep_awake_while_streaming"),
        videoSettings.keep_awake_while_streaming,
        [](bool value)
        {
            ConfigManager config;
            config.updateVideoSettings([value](VideoSettings& settings)
                { settings.keep_awake_while_streaming = value; });
            SessionMainView::setKeepAwakeWhileStreaming(value);
        });

    bool configChanged = false;
    if (videoSettings.render_mode == 0)
    {
        if (videoSettings.pixel_format_mode != 0 && videoSettings.pixel_format_mode != 1)
        {
            videoSettings.pixel_format_mode = 1;
            configChanged                   = true;
        }
    }
    else if (videoSettings.render_mode == 1)
    {
        if (videoSettings.pixel_format_mode != 1)
        {
            videoSettings.pixel_format_mode = 1;
            configChanged                   = true;
        }
    }
    const auto normalizedDisplayMode = moonlight::settings::normalizeStreamDisplayMode(
        videoSettings.swap_interval, videoSettings.render_mode);
    if (videoSettings.swap_interval != static_cast<int>(normalizedDisplayMode))
    {
        videoSettings.swap_interval = static_cast<int>(normalizedDisplayMode);
        configChanged               = true;
    }
    if (configChanged)
    {
        const int pixelFormat = videoSettings.pixel_format_mode;
        const int displayMode = videoSettings.swap_interval;
        config.updateVideoSettings([pixelFormat, displayMode](VideoSettings& settings)
            {
                settings.pixel_format_mode = pixelFormat;
                settings.swap_interval     = displayMode; });
    }
    video_runtime::setPixelFormatMode(videoSettings.pixel_format_mode);

    if (videoSettings.save_debug_log)
    {
        enable_file_logging(true);
    }

    vita_netopt_set_enabled(videoSettings.enable_network_optimizations ? 1 : 0);

    std::vector<std::string> renderModes;
    renderModes.push_back(brls::getStr("moonlight/settings_tab/render_mode/legacy_option"));
    renderModes.push_back(brls::getStr("moonlight/settings_tab/render_mode/modern_option"));
    auto updateModeDependentVisibility = [this](int renderMode)
    {
        if (pixelFormatSelector)
        {
            if (renderMode == 0)
            {
                pixelFormatSelector->setVisibility(brls::Visibility::VISIBLE);
            }
            else
            {
                pixelFormatSelector->setVisibility(brls::Visibility::GONE);
            }
        }
    };

    int initialRenderMode = videoSettings.render_mode;
    if (initialRenderMode < 0 || initialRenderMode >= (int)renderModes.size())
        initialRenderMode = 0;
    renderModeSelector->init(brls::getStr("moonlight/settings_tab/render_mode/title"), renderModes, initialRenderMode, [this, updateModeDependentVisibility](int selected)
        {
        ConfigManager config;
        int chosen = selected;
        int pixelFormat = 1;
        int displayMode = static_cast<int>(moonlight::StreamDisplayMode::NextFrame);
        config.updateVideoSettings([chosen, &pixelFormat, &displayMode](VideoSettings& settings) {
            settings.render_mode = chosen;
            if (chosen == 0) {
                if (settings.pixel_format_mode != 0 && settings.pixel_format_mode != 1) {
                    settings.pixel_format_mode = 1;
                }
            } else if (chosen == 1) {
                settings.pixel_format_mode = 1;
            }
            settings.swap_interval = static_cast<int>(
                moonlight::settings::normalizeStreamDisplayMode(settings.swap_interval, chosen));
            pixelFormat = settings.pixel_format_mode;
            displayMode = settings.swap_interval;
        });
        updateModeDependentVisibility(chosen);
        updateDisplayModeOptions(chosen, displayMode);
        video_runtime::setRenderMode(chosen);
        video_runtime::setPixelFormatMode(pixelFormat);
        const char* modeNameKey = nullptr;
        if (chosen == 0) {
            modeNameKey = "moonlight/settings_tab/render_mode/legacy_name";
        }
        else if (chosen == 1) {
            modeNameKey = "moonlight/settings_tab/render_mode/modern_name";
        }
        else {
            modeNameKey = "moonlight/settings_tab/render_mode/unknown_name";
        }
        brls::Application::notify(
            brls::getStr("moonlight/settings_tab/render_mode/notify", brls::getStr(modeNameKey))); });

    std::vector<std::string> pixelFormats = {
        brls::getStr("moonlight/settings_tab/pixel_format/rgba"),
        brls::getStr("moonlight/settings_tab/pixel_format/yuv")
    };
    pixelFormatSelector->init(brls::getStr("moonlight/settings_tab/pixel_format/title"), pixelFormats, videoSettings.pixel_format_mode, [pixelFormats](int selected)
        {
        ConfigManager config;
        config.updateVideoSettings([selected](VideoSettings& settings) { settings.pixel_format_mode = selected; });
        video_runtime::setPixelFormatMode(selected);
        const std::string& label = pixelFormats.at(static_cast<std::size_t>(selected));
        brls::Application::notify(
            brls::getStr("moonlight/settings_tab/pixel_format/notify", label)); });

    updateModeDependentVisibility(initialRenderMode);

    const auto& streamResolutionValues = stream::supportedStreamResolutions();
    std::vector<std::string> streamResolutions;
    streamResolutions.reserve(streamResolutionValues.size());
    for (std::size_t i = 0; i < streamResolutionValues.size(); ++i)
    {
        streamResolutions.push_back(brls::getStr(
            "moonlight/settings_tab/stream_resolution/options/" + std::to_string(i)));
    }

    int currentStreamResolution = static_cast<int>(stream::streamResolutionIndex(
        streamConfig.streamWidth, streamConfig.streamHeight));
    streamResolutionSelector->init(
        brls::getStr("moonlight/settings_tab/stream_resolution/title"),
        streamResolutions,
        currentStreamResolution,
        [](int selected)
        {
            const auto& streamResolutionValues = stream::supportedStreamResolutions();
            if (selected < 0 || selected >= static_cast<int>(streamResolutionValues.size()))
            {
                return;
            }

            ConfigManager config;
            const auto resolution = streamResolutionValues[selected];
            config.updateStreamConfig([resolution](StreamConfiguration& settings)
                {
                settings.streamWidth  = resolution.width;
                settings.streamHeight = resolution.height; });
            brls::Application::notify(
                brls::getStr("moonlight/settings_tab/stream_resolution/saved"));
        });

    std::vector<std::string> resolutions = {
        brls::getStr("moonlight/settings_tab/resolution/options/0"),
        brls::getStr("moonlight/settings_tab/resolution/options/1"),
        brls::getStr("moonlight/settings_tab/resolution/options/2"),
        brls::getStr("moonlight/settings_tab/resolution/options/3"),
        brls::getStr("moonlight/settings_tab/resolution/options/4"),
        brls::getStr("moonlight/settings_tab/resolution/options/5"),
        brls::getStr("moonlight/settings_tab/resolution/options/6"),
        brls::getStr("moonlight/settings_tab/resolution/options/7"),
        brls::getStr("moonlight/settings_tab/resolution/options/8")
    };

    const auto& hostResolutions = stream::supportedHostResolutions();
    int currentRes              = static_cast<int>(stream::hostResolutionIndex(streamConfig.width, streamConfig.height));

    resolutionSelector->init(brls::getStr("moonlight/settings_tab/resolution/title"), resolutions, currentRes, [hostResolutions](int selected)
        {
        if (selected < 0 || selected >= static_cast<int>(hostResolutions.size())) {
            return;
        }

        ConfigManager config;
        const auto resolution = hostResolutions[selected];
        config.updateStreamConfig([resolution](StreamConfiguration& streamConfig) {
            streamConfig.width  = resolution.width;
            streamConfig.height = resolution.height;
        });
        brls::Application::notify(brls::getStr("moonlight/settings_tab/resolution/saved")); });

    std::vector<std::string> fpsOptions = {
        brls::getStr("moonlight/settings_tab/fps/options/0"),
        brls::getStr("moonlight/settings_tab/fps/options/1"),
        brls::getStr("moonlight/settings_tab/fps/options/2"),
        brls::getStr("moonlight/settings_tab/fps/options/3"),
        brls::getStr("moonlight/settings_tab/fps/options/4")
    };
    int currentFps = static_cast<int>(stream::frameRateIndex(streamConfig.fps));

    fpsSelector->init(brls::getStr("moonlight/settings_tab/fps/title"), fpsOptions, currentFps, [](int selected)
        {
        ConfigManager config;
        const auto& fpsValues = stream::supportedFrameRates();
        if (selected < 0 || selected >= static_cast<int>(fpsValues.size()))
            return;
        const int fps = fpsValues[selected];
        config.updateStreamConfig([fps](StreamConfiguration& streamConfig) { streamConfig.fps = fps; });
        brls::Application::notify(brls::getStr("moonlight/settings_tab/fps/saved")); });

    std::vector<std::string> bitrateOptions = {
        brls::getStr("moonlight/settings_tab/bitrate/options/0"),
        brls::getStr("moonlight/settings_tab/bitrate/options/1"),
        brls::getStr("moonlight/settings_tab/bitrate/options/2"),
        brls::getStr("moonlight/settings_tab/bitrate/options/3"),
        brls::getStr("moonlight/settings_tab/bitrate/options/4"),
        brls::getStr("moonlight/settings_tab/bitrate/options/5"),
        brls::getStr("moonlight/settings_tab/bitrate/options/6"),
        brls::getStr("moonlight/settings_tab/bitrate/options/7"),
        brls::getStr("moonlight/settings_tab/bitrate/options/8")
    };

    int currentBitrate = static_cast<int>(stream::bitrateIndex(streamConfig.bitrate));

    bitrateSelector->init(brls::getStr("moonlight/settings_tab/bitrate/title"), bitrateOptions, currentBitrate, [](int selected)
        {
        ConfigManager config;
        const auto& bitrateValues = stream::supportedBitrates();
        if (selected < 0 || selected >= static_cast<int>(bitrateValues.size()))
            return;
        const int bitrate = bitrateValues[selected];
        config.updateStreamConfig([bitrate](StreamConfiguration& streamConfig) { streamConfig.bitrate = bitrate; });
        brls::Application::notify(brls::getStr("moonlight/settings_tab/bitrate/saved")); });

    sopsToggle->setVisibility(brls::Visibility::GONE);
    if (!videoSettings.sops)
    {
        ConfigManager config;
        config.updateVideoSettings([](VideoSettings& settings)
            { settings.sops = true; });
    }
    networkOptimizationsToggle->init(brls::getStr("moonlight/settings_tab/network_opt_title"), videoSettings.enable_network_optimizations, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.enable_network_optimizations = value; });
        vita_netopt_set_enabled(value ? 1 : 0);
        brls::Application::notify(brls::getStr(value ? "moonlight/settings_tab/network_opt_enabled" : "moonlight/settings_tab/network_opt_disabled")); });

    localAudioToggle->init(brls::getStr("moonlight/settings_tab/local_audio_title"), videoSettings.localaudio, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.localaudio = value; }); });

    fullscreenToggle->init(brls::getStr("moonlight/settings_tab/fullscreen_title"), videoSettings.fullscreen, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.fullscreen = value; }); });

    framePacerToggle->init(brls::getStr("moonlight/settings_tab/frame_pacer_title"), videoSettings.enable_frame_pacer, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.enable_frame_pacer = value; });

        auto session = VitaSession::active();
        if (session && session->isActive()) {
            const VideoSettings videoSettings = config.getVideoSettings();
            const StreamConfiguration stream  = config.getStreamConfig();
            moonlight::settings::applyStreamDisplayMode(
                videoSettings.swap_interval, videoSettings.render_mode, value, stream.fps);
        } });

    centerRegionToggle->init(brls::getStr("moonlight/settings_tab/center_region_title"), videoSettings.center_region_only, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.center_region_only = value; }); });

    showFpsToggle->init(brls::getStr("moonlight/settings_tab/show_fps_title"), videoSettings.show_fps, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.show_fps = value; });
        video_runtime::setShowFps(value); });

    debugLogToggle->init(brls::getStr("moonlight/settings_tab/save_debug_log_title"), videoSettings.save_debug_log, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.save_debug_log = value; });
        enable_file_logging(value); });

    if (gyroSettingsEntry)
    {
        gyroSettingsEntry->setDetailText(brls::getStr("moonlight/gyro/settings_detail"));
        gyroSettingsEntry->registerClickAction([](brls::View*)
            {
            auto* gyroView = new GyroSettingsTab();
            moonlight::settings::pushView(gyroView, brls::getStr("moonlight/gyro/title"));
            return true; });
    }

    doubleTapSprintToggle->init(brls::getStr("moonlight/settings_tab/double_tap_sprint_title"), videoSettings.enable_double_tap_sprint, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.enable_double_tap_sprint = value; }); });

    if (rearTouchSettingsEntry)
    {
        rearTouchSettingsEntry->setDetailText(brls::getStr("moonlight/settings/rear_touch_detail"));
        rearTouchSettingsEntry->registerClickAction([](brls::View*)
            {
            auto* rearTouchView = new RearTouchSettingsTab();
            moonlight::settings::pushView(rearTouchView, brls::getStr("moonlight/rear_touch/title"));
            return true; });
    }

    if (frontTouchSettingsEntry)
    {
        frontTouchSettingsEntry->setDetailText(brls::getStr("moonlight/settings/front_touch_detail"));
        frontTouchSettingsEntry->registerClickAction([](brls::View*)
            {
            auto* frontTouchView = new FrontTouchSettingsTab();
            moonlight::settings::pushView(frontTouchView, brls::getStr("moonlight/front_touch/title"));
            return true; });
    }

    if (trackpadSettingsEntry)
    {
        trackpadSettingsEntry->setDetailText(brls::getStr("moonlight/trackpad/header"));
        trackpadSettingsEntry->registerClickAction([](brls::View*)
            {
            auto* trackpadView = new TrackpadSettingsTab();
            moonlight::settings::pushView(trackpadView, brls::getStr("moonlight/trackpad/header"));
            return true; });
    }

    std::vector<std::string> touchscreenModes = {
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/0"),
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/1"),
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/2"),
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/3"),
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/4")
    };
    touchscreenModeSelector->init(brls::getStr("moonlight/settings_tab/touchscreen_mode/title"), touchscreenModes, videoSettings.touchscreen_mode, [touchscreenModes](int selected)
        {
        if (g_controllerInput && g_controllerInput->setTouchscreenModeRuntime(selected)) {
            std::string message = brls::getStr("moonlight/settings_tab/touchscreen_mode/changed") +
                                  " " + touchscreenModes.at(selected);
            brls::Application::notify(message);
        } else {
            if (selected == TOUCHSCREEN_MODE_DS4_TOUCHPAD) {
                brls::Application::notify(brls::getStr("moonlight/settings_tab/touchscreen_mode/ds4_requires_playstation"));
            } else {
                brls::Application::notify(brls::getStr("moonlight/settings_tab/touchscreen_mode/change_failed"));
            }
        } });

    std::vector<std::string> gamepadTypes = {
        brls::getStr("moonlight/settings_tab/gamepad_type/options/xbox"),
        brls::getStr("moonlight/settings_tab/gamepad_type/options/ps4")
    };
    gamepadTypeSelector->init(brls::getStr("moonlight/settings_tab/gamepad_type/title"), gamepadTypes, (int)videoSettings.gamepad_type, [gamepadTypes](int selected)
        {
        ConfigManager config;
        GamepadType newType = (selected == 0) ? GAMEPAD_TYPE_XBOX : GAMEPAD_TYPE_PS4;
        config.updateVideoSettings([newType](VideoSettings& settings) { settings.gamepad_type = newType; });
        if (g_controllerInput) {
            g_controllerInput->setGamepadType(newType);
            std::string message = brls::getStr("moonlight/settings_tab/gamepad_type/notify_prefix") +
                                  gamepadTypes.at(selected);
            brls::Application::notify(message);
        } });

    psButtonCaptureToggle->init(
        brls::getStr("moonlight/settings_tab/capture_ps_button"),
        videoSettings.capture_ps_button,
        [](bool value)
        {
            ConfigManager config;
            config.updateVideoSettings([value](VideoSettings& settings)
                { settings.capture_ps_button = value; });
            if (g_controllerInput)
            {
                g_controllerInput->setPSButtonCapture(value);
            }
        });

    swapShoulderButtonsToggle->init(brls::getStr("moonlight/settings_tab/swap_shoulder_buttons/title"), videoSettings.swap_shoulder_buttons, [](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.swap_shoulder_buttons = value; });
        if (g_controllerInput) {
            g_controllerInput->setSwapShoulderButtons(value);
        }
        brls::Application::notify(brls::getStr(value
            ? "moonlight/settings_tab/swap_shoulder_buttons/notify_enabled"
            : "moonlight/settings_tab/swap_shoulder_buttons/notify_disabled")); });

    if (keyboardConfigureCell)
    {
        keyboardConfigureCell->setDetailText(brls::getStr("moonlight/keyboard/configure_detail"));
        keyboardConfigureCell->registerClickAction([](brls::View*)
            {
            auto* keyboardView = new KeyboardSettingsTab();
            moonlight::settings::pushView(keyboardView, brls::getStr("moonlight/keyboard/title"));
            return true; });
    }

    if (shortcutsConfigureCell)
    {
        shortcutsConfigureCell->setDetailText(brls::getStr("moonlight/shortcuts/configure_detail"));
        shortcutsConfigureCell->registerClickAction([](brls::View*)
            {
            auto* shortcutsView = new ShortcutsSettingsTab();
            moonlight::settings::pushView(shortcutsView, brls::getStr("moonlight/shortcuts/title"));
            return true; });
    }

    microphoneToggle->init(
        brls::getStr("moonlight/settings_tab/microphone_enabled_title"),
        videoSettings.enable_microphone,
        [](bool value)
        {
            ConfigManager config;
            config.updateVideoSettings([value](VideoSettings& settings)
                { settings.enable_microphone = value; });

            brls::Application::notify(
                value ? brls::getStr("moonlight/settings_tab/microphone_enabled")
                      : brls::getStr("moonlight/settings_tab/microphone_disabled"));
        });

    microphoneConfigureCell->registerClickAction([](brls::View*)
        {
        auto* micView = new MicrophoneSettingsTab();
        moonlight::settings::pushView(micView, brls::getStr("moonlight/microphone/title"));
        return true; });

    ipAddress->setDetailText("...");

    slider->init("Brightness", 0.0f, [this](float value)
        {
        brls::Application::getPlatform()->setBacklightBrightness(value);
        slider->setDetailText(fmt::format("{:.2f}", value)); });
    slider->setDetailText("...");

    std::vector<std::string> displayModeLabels;
    const int currentDisplayMode = prepareDisplayModeOptions(
        videoSettings.render_mode, videoSettings.swap_interval, displayModeLabels);
    swapInterval->init(brls::getStr("moonlight/settings_tab/swap_interval/title"), displayModeLabels, currentDisplayMode, [this](int selected)
        {
        if (selected >= 0 && static_cast<std::size_t>(selected) < displayModeValues.size()) {
            const int displayMode = displayModeValues[selected];
            ConfigManager config;
            config.updateVideoSettings([displayMode](VideoSettings& settings) { settings.swap_interval = displayMode; });

            auto session = VitaSession::active();
            if (session && session->isActive()) {
                const VideoSettings videoSettings = config.getVideoSettings();
                const StreamConfiguration stream  = config.getStreamConfig();
                moonlight::settings::applyStreamDisplayMode(
                    displayMode, videoSettings.render_mode, videoSettings.enable_frame_pacer, stream.fps);
            }
            brls::Application::notify(brls::getStr("moonlight/settings_tab/swap_interval/saved"));
        } });

    this->initAsync();

    std::vector<std::string> languages;
    const auto& languageOptions = moonlight::settings::supportedLanguages();
    languages.reserve(languageOptions.size());
    for (const auto& option : languageOptions)
        languages.emplace_back(option.label);

    int currentLang = static_cast<int>(moonlight::settings::languageIndex(brls::Application::getLocale()));
    languageSelector->init(brls::getStr("moonlight/settings/language"), languages, currentLang, [](int selected)
        {
            const auto& languageOptions = moonlight::settings::supportedLanguages();
            if (selected < 0 || static_cast<std::size_t>(selected) >= languageOptions.size())
                return;
            std::string locale = languageOptions[selected].locale;

            std::string configPath = ConfigManager::getConfigPath();
            size_t pos             = configPath.find_last_of("/\\");
            if (pos != std::string::npos)
            {
                std::string configDir = configPath.substr(0, pos);
#ifdef _WIN32
                CreateDirectoryA(configDir.c_str(), NULL);
#else
                mkdir(configDir.c_str(), 0755);
#endif
            }

            ConfigManager config;
            config.load();
            config.set("general", "language", locale);
            if (!config.save())
            {
                brls::Application::notify(brls::getStr("moonlight/settings_tab/language_save_failed"));
                return;
            }

#if defined(__PSV__)
            brls::Application::notify(brls::getStr("moonlight/settings_tab/language_restarting"));
            brls::delay(300, []()
                {
            int result = sceAppMgrLoadExec("app0:eboot.bin", nullptr, nullptr);
            if (result < 0) {
                vita_log::error("[Settings] sceAppMgrLoadExec fallo=0x%X", result);
                brls::Application::notify(brls::getStr("moonlight/settings_tab/language_restart_failed"));
            } });
#else
            brls::Application::notify(brls::getStr("moonlight/settings_tab/language_changed"));
#endif
        });
}

SettingsTab::~SettingsTab()
{
    asyncState->alive.store(false, std::memory_order_release);
    if (asyncState->opacityDirty.exchange(false, std::memory_order_acq_rel))
    {
        saveSettingsOpacity(asyncState->pendingOpacity.load(std::memory_order_acquire));
    }
}

brls::View* SettingsTab::create()
{
    return new SettingsTab();
}

void SettingsTab::initAsync()
{
    std::shared_ptr<AsyncState> state = this->asyncState;

    std::thread([this, state]()
        {
        std::string ip = brls::Application::getPlatform()->getIpAddress();
        float brightness = brls::Application::getPlatform()->getBacklightBrightness();

        brls::sync([this, state, ip, brightness]() {
            if (!state->alive.load(std::memory_order_acquire)) return;

            ipAddress->setDetailText(ip);
            slider->init("Brightness", brightness, [this](float value){
                brls::Application::getPlatform()->setBacklightBrightness(value);
                slider->setDetailText(fmt::format("{:.2f}", value));
            });
            slider->setDetailText(fmt::format("{:.2f}", brightness));
        }); })
        .detach();
}
