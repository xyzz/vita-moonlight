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

#include <cstdlib>
#include <memory>
#include <string>

#include "ConfigManager.hpp"
#include "controller/ControllerInput.hpp"
#include "tab/front_touch_settings_tab.hpp"
#include "tab/gyro_settings_tab.hpp"
#include "tab/keyboard_settings_tab.hpp" // NUEVO
#include "tab/microphone_settings_tab.hpp"
#include "tab/rear_touch_settings_tab.hpp"
#include "tab/shortcuts_settings_tab.hpp"
#include "tab/trackpad_settings_tab.hpp"
#ifndef _WIN32
#include <sys/stat.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif
#include "audio/MicrophoneTester.hpp"
#include "network/NetworkOptimizations.hpp"
#include "settings.hpp"
#include "video/render_mode_cache.hpp"

using namespace brls::literals; // for _i18n

#include "debug.hpp"

bool radioSelected = false;

static std::vector<std::string> NOTIFICATIONS = {
    "You have cool hair",
    "I like your shoes",
    "borealis is powered by nanovg",
    "The Triforce is an inside job",
    "Pozznx will trigger in one day and twelve hours",
    "Aurora Borealis? At this time of day, at this time of year, in this part of the gaming market, located entirely within your Switch?!",
    "May I see it?",
    "Hmm, Steamed Hams!",
    "Hello\nWorld!"
};

SettingsTab::SettingsTab()
{
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/settings.xml");

    ConfigManager config;
    config.load();
    StreamConfiguration streamConfig = config.getStreamConfig();
    VideoSettings videoSettings      = config.getVideoSettings();

    // Auto-synchronize correct format settings on startup
    bool configChanged = false;
    if (videoSettings.render_mode == 0)
    {
        if (videoSettings.pixel_format_mode != 0 && videoSettings.pixel_format_mode != 1)
        {
            videoSettings.pixel_format_mode = 1; // Default to YUV for high performance
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
    if (configChanged)
    {
        config.setVideoSettings(videoSettings);
        config.save();
    }
    extern VideoSettings g_video_settings_snapshot;
    g_video_settings_snapshot.pixel_format_mode = videoSettings.pixel_format_mode;

    // Initialize global flag for debug logs
    extern bool g_debug_log_enabled;
    g_debug_log_enabled = videoSettings.save_debug_log;
    if (videoSettings.save_debug_log)
    {
        enable_file_logging(true);
    }

    // Initialize network optimizations
    vita_netopt_set_enabled(videoSettings.enable_network_optimizations ? 1 : 0);

    // Render mode selector (Direct GXM removed): 0=Legacy, 1=FFmpeg (future)
    std::vector<std::string> renderModes;
    renderModes.push_back(brls::getStr("moonlight/settings_tab/render_mode/legacy_option"));
    renderModes.push_back(brls::getStr("moonlight/settings_tab/render_mode/modern_option"));
    auto updateModeDependentVisibility = [this](int renderMode, bool persistReset)
    {
        (void)persistReset;

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
        initialRenderMode = 0; // clamp if config has unknown value
    // Shared container to keep the mapping from visible selector indices to original vitaResolutions
    auto filteredIndicesPtr = std::make_shared<std::vector<int>>();
    renderModeSelector->init(brls::getStr("moonlight/settings_tab/render_mode/title"), renderModes, initialRenderMode, [this, updateModeDependentVisibility, filteredIndicesPtr, streamConfig](int selected)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        int chosen = selected; // allow modern/ffmpeg
        settings.render_mode = chosen; // 0=legacy,1=ffmpeg
        if (chosen == 0) {
            if (settings.pixel_format_mode != 0 && settings.pixel_format_mode != 1) {
                settings.pixel_format_mode = 1; // Default to YUV for high performance
            }
        } else if (chosen == 1) {
            settings.pixel_format_mode = 1; // FFmpeg uses YUV
        }
        config.setVideoSettings(settings);
        config.save();
        // Update atomic cache without subsequent re-reading
        set_render_mode_cached(chosen);
        updateModeDependentVisibility(chosen, true);
        extern VideoSettings g_video_settings_snapshot;
        g_video_settings_snapshot.render_mode = chosen;
        g_video_settings_snapshot.pixel_format_mode = settings.pixel_format_mode;
        // Also update available resolution options depending on render mode
    // Legacy mode (0) only exposes resolutions <= 1280x720. FFmpeg (modern) exposes all.
    auto updateResOptions = [this, filteredIndicesPtr](int mode, const StreamConfiguration& sc) {
            // Recreate the vitaResolutions and labels exactly as initial construction in this TU
            std::vector<std::string> resolutions = {
                brls::getStr("moonlight/settings_tab/resolution/options/0"),  // Auto
                brls::getStr("moonlight/settings_tab/resolution/options/1"),  // 960x544
                brls::getStr("moonlight/settings_tab/resolution/options/2"),
                brls::getStr("moonlight/settings_tab/resolution/options/3"),
                brls::getStr("moonlight/settings_tab/resolution/options/4"),
                brls::getStr("moonlight/settings_tab/resolution/options/5"),  // 1280x720
                brls::getStr("moonlight/settings_tab/resolution/options/6"),
                brls::getStr("moonlight/settings_tab/resolution/options/7"),
                brls::getStr("moonlight/settings_tab/resolution/options/8")   // 1920x1080
            };
            std::vector<std::pair<int, int>> vitaResolutions = {
                {0, 0},       // Auto (host current)
                {960, 544},   // Vita native (recommended)
                {1024, 576},
                {1152, 656},
                {1280, 544},
                {1280, 720},
                {1360, 768},
                {1600, 896},
                {1920, 1080}
            };

            std::vector<std::string> filteredLabels;
            std::vector<int> filteredIndices;
            for (size_t i = 0; i < vitaResolutions.size(); ++i) {
                int w = vitaResolutions[i].first;
                int h = vitaResolutions[i].second;
                bool include = true;
                if (mode == 0) { // legacy
                    // only include resolutions 1280x720 and below
                    if (w > 1920 || h > 1080) include = false; // test upper bound
                }
                if (include) {
                    filteredLabels.push_back(resolutions[i]);
                    filteredIndices.push_back((int)i);
                }
            }

            if (resolutionSelector) {
                // update shared mapping
                filteredIndicesPtr->assign(filteredIndices.begin(), filteredIndices.end());
                resolutionSelector->setData(filteredLabels);
                // Determine selection based on current saved stream config
                int mapped = 0;
                // try to find saved resolution in the filtered list
                for (size_t i = 0; i < filteredIndices.size(); ++i) {
                    int orig = filteredIndices[i];
                    if (vitaResolutions[orig].first == sc.width && vitaResolutions[orig].second == sc.height) {
                        mapped = (int)i;
                        break;
                    }
                }
                // if not found, prefer 1280x720
                bool foundPref = false;
                for (size_t i = 0; i < filteredIndices.size(); ++i) {
                    if (filteredIndices[i] == 5) { mapped = (int)i; foundPref = true; break; }
                }
                if (!foundPref && filteredIndices.empty() == false) mapped = 0;
                resolutionSelector->setSelection(mapped, true);
            }
        };
        updateResOptions(chosen, streamConfig);
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

    // Pixel format selector (for RGBA vs YUV tests)
    std::vector<std::string> pixelFormats = {
        brls::getStr("moonlight/settings_tab/pixel_format/rgba"),
        brls::getStr("moonlight/settings_tab/pixel_format/yuv")
    };
    pixelFormatSelector->init(brls::getStr("moonlight/settings_tab/pixel_format/title"), pixelFormats, videoSettings.pixel_format_mode, [this, pixelFormats](int selected)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.pixel_format_mode = selected;
        config.setVideoSettings(settings);
        config.save();
        extern VideoSettings g_video_settings_snapshot;
        g_video_settings_snapshot.pixel_format_mode = selected;
        const std::string& label = pixelFormats.at(static_cast<std::size_t>(selected));
        brls::Application::notify(
            brls::getStr("moonlight/settings_tab/pixel_format/notify", label)); });

    updateModeDependentVisibility(initialRenderMode, false);

    // Set resolution selectors with allowed values ​​for PS Vita
    std::vector<std::string> resolutions = {
        brls::getStr("moonlight/settings_tab/resolution/options/0"), // Auto (host current)
        brls::getStr("moonlight/settings_tab/resolution/options/1"), // 960x544 recommended
        brls::getStr("moonlight/settings_tab/resolution/options/2"),
        brls::getStr("moonlight/settings_tab/resolution/options/3"),
        brls::getStr("moonlight/settings_tab/resolution/options/4"),
        brls::getStr("moonlight/settings_tab/resolution/options/5"), // 1280x720
        brls::getStr("moonlight/settings_tab/resolution/options/6"),
        brls::getStr("moonlight/settings_tab/resolution/options/7"),
        brls::getStr("moonlight/settings_tab/resolution/options/8") // 1920x1080
    };

    // Resolutions allowed for PS Vita / HOST control
    std::vector<std::pair<int, int>> vitaResolutions = {
        { 0, 0 }, // Auto (host keeps current resolution)
        { 960, 544 }, // Vita native (recommended for best quality)
        { 1024, 576 },
        { 1152, 656 },
        { 1280, 544 },
        { 1280, 720 },
        { 1360, 768 },
        { 1600, 896 },
        { 1920, 1080 }
    };

    // Default to Auto (index 0) or 960x544 (index 1)
    int currentRes = 0; // Default to Auto
    if (streamConfig.width == 0 && streamConfig.height == 0)
        currentRes = 0; // Auto
    else if (streamConfig.width == 960 && streamConfig.height == 544)
        currentRes = 1;
    else if (streamConfig.width == 1024 && streamConfig.height == 576)
        currentRes = 2;
    else if (streamConfig.width == 1152 && streamConfig.height == 656)
        currentRes = 3;
    else if (streamConfig.width == 1280 && streamConfig.height == 544)
        currentRes = 4;
    else if (streamConfig.width == 1280 && streamConfig.height == 720)
        currentRes = 5;
    else if (streamConfig.width == 1360 && streamConfig.height == 768)
        currentRes = 6;
    else if (streamConfig.width == 1600 && streamConfig.height == 896)
        currentRes = 7;
    else if (streamConfig.width == 1920 && streamConfig.height == 1080)
        currentRes = 8;

    // Build an initial filtered list of resolutions depending on the initial render mode.
    auto buildFiltered = [&](int mode, std::vector<std::string>& outLabels, std::vector<int>& outIndices)
    {
        outLabels.clear();
        outIndices.clear();
        for (size_t i = 0; i < vitaResolutions.size(); ++i)
        {
            int w        = vitaResolutions[i].first;
            int h        = vitaResolutions[i].second;
            bool include = true;
            if (mode == 0)
            { // legacy -> only <= 1280x720
                if (w > 1920 || h > 1080)
                    include = false;
            }
            if (include)
            {
                outLabels.push_back(resolutions[i]);
                outIndices.push_back((int)i);
            }
        }
    };

    std::vector<std::string> initialLabels;
    std::vector<int> initialIndices;
    buildFiltered(initialRenderMode, initialLabels, initialIndices);

    // Map original currentRes to filtered index
    int mappedInitial = 0;
    auto itInit       = std::find(initialIndices.begin(), initialIndices.end(), currentRes);
    if (itInit != initialIndices.end())
        mappedInitial = (int)std::distance(initialIndices.begin(), itInit);
    else
    {
        // prefer the 1280x720 option when available
        int pref    = 5;
        auto itPref = std::find(initialIndices.begin(), initialIndices.end(), pref);
        if (itPref != initialIndices.end())
            mappedInitial = (int)std::distance(initialIndices.begin(), itPref);
        else
            mappedInitial = 0;
    }

    // Use the previously declared shared vector to keep filtered original indices up-to-date for the callback
    filteredIndicesPtr->assign(initialIndices.begin(), initialIndices.end());

    resolutionSelector->init(brls::getStr("moonlight/settings_tab/resolution/title"), initialLabels, mappedInitial, [this, vitaResolutions, filteredIndicesPtr](int selected)
        {
        ConfigManager config;
        config.load();
        StreamConfiguration streamConfig = config.getStreamConfig();

        if (selected >= 0 && selected < (int)filteredIndicesPtr->size()) {
            int origIndex = (*filteredIndicesPtr)[selected];
            streamConfig.width = vitaResolutions[origIndex].first;
            streamConfig.height = vitaResolutions[origIndex].second;
            // Apply PS Vita validation
            streamConfig.validateAndAdjustResolution();
        }

        config.setStreamConfig(streamConfig);
        config.save();
        brls::Application::notify(brls::getStr("moonlight/settings_tab/resolution/saved")); });

    // Configure FPS selector with legacy values
    std::vector<std::string> fpsOptions = {
        brls::getStr("moonlight/settings_tab/fps/options/0"),
        brls::getStr("moonlight/settings_tab/fps/options/1"),
        brls::getStr("moonlight/settings_tab/fps/options/2"),
        brls::getStr("moonlight/settings_tab/fps/options/3"),
        brls::getStr("moonlight/settings_tab/fps/options/4")
    };
    int currentFps = 4; // Default 60 FPS (index 4)
    if (streamConfig.fps == 24)
        currentFps = 0;
    else if (streamConfig.fps == 30)
        currentFps = 1;
    else if (streamConfig.fps == 40)
        currentFps = 2;
    else if (streamConfig.fps == 50)
        currentFps = 3;

    fpsSelector->init(brls::getStr("moonlight/settings_tab/fps/title"), fpsOptions, currentFps, [this](int selected)
        {
        ConfigManager config;
        config.load();
        StreamConfiguration streamConfig = config.getStreamConfig();

        switch (selected) {
            case 0: streamConfig.fps = 24; break; // Cine
            case 1: streamConfig.fps = 30; break; // Standard
            case 2: streamConfig.fps = 40; break;
            case 3: streamConfig.fps = 50; break; // PAL
            case 4: streamConfig.fps = 60; break; // NTSC
        }
        config.setStreamConfig(streamConfig);
        config.save();
        brls::Application::notify(brls::getStr("moonlight/settings_tab/fps/saved")); });

    // Configure bitrate selector with appropriate values ​​for PS Vita
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

    int currentBitrate = 0; // Auto by default
    if (streamConfig.bitrate == 2000)
        currentBitrate = 1;
    else if (streamConfig.bitrate == 5000)
        currentBitrate = 2;
    else if (streamConfig.bitrate == 8000)
        currentBitrate = 3;
    else if (streamConfig.bitrate == 10000)
        currentBitrate = 4;
    else if (streamConfig.bitrate == 15000)
        currentBitrate = 5;
    else if (streamConfig.bitrate == 20000)
        currentBitrate = 6;
    else if (streamConfig.bitrate == 30000)
        currentBitrate = 7;
    else if (streamConfig.bitrate == 50000)
        currentBitrate = 8;

    bitrateSelector->init(brls::getStr("moonlight/settings_tab/bitrate/title"), bitrateOptions, currentBitrate, [this](int selected)
        {
        ConfigManager config;
        config.load();
        StreamConfiguration streamConfig = config.getStreamConfig();

        switch (selected) {
            case 0: streamConfig.bitrate = -1; break; // Auto
            case 1: streamConfig.bitrate = 2000; break;
            case 2: streamConfig.bitrate = 5000; break;
            case 3: streamConfig.bitrate = 8000; break;
            case 4: streamConfig.bitrate = 10000; break;
            case 5: streamConfig.bitrate = 15000; break;
            case 6: streamConfig.bitrate = 20000; break;
            case 7: streamConfig.bitrate = 30000; break;
            case 8: streamConfig.bitrate = 50000; break;
        }
        config.setStreamConfig(streamConfig);
        config.save();
        brls::Application::notify(brls::getStr("moonlight/settings_tab/bitrate/saved")); });

    // SOPS (Sound Over PS Network) - Force Enabled for MoonMic compatibility
    // Hidden from UI to prevent accidental disable
    sopsToggle->setVisibility(brls::Visibility::GONE);
    if (!videoSettings.sops)
    {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.sops          = true;
        config.setVideoSettings(settings);
        config.save();
    }
    /*
    sopsToggle->init(brls::getStr("moonlight/settings_tab/sops_title"), videoSettings.sops, [this](bool value) {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.sops = value;
        config.setVideoSettings(settings);
        config.save();
    });
    */

    // Toggle for network optimizations (IDR smart, pacing, etc.)
    networkOptimizationsToggle->init(brls::getStr("moonlight/settings_tab/network_opt_title"), videoSettings.enable_network_optimizations, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.enable_network_optimizations = value;
        config.setVideoSettings(settings);
        config.save();
        // Apply immediately
        vita_netopt_set_enabled(value ? 1 : 0);
        brls::Application::notify(brls::getStr(value ? "moonlight/settings_tab/network_opt_enabled" : "moonlight/settings_tab/network_opt_disabled")); });

    localAudioToggle->init(brls::getStr("moonlight/settings_tab/local_audio_title"), videoSettings.localaudio, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.localaudio = value;
        config.setVideoSettings(settings);
        config.save(); });

    fullscreenToggle->init(brls::getStr("moonlight/settings_tab/fullscreen_title"), videoSettings.fullscreen, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.fullscreen = value;
        config.setVideoSettings(settings);
        config.save(); });

    // Low Latency removed: toggle removed

    framePacerToggle->init(brls::getStr("moonlight/settings_tab/frame_pacer_title"), videoSettings.enable_frame_pacer, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.enable_frame_pacer = value;
        config.setVideoSettings(settings);
        config.save();
        extern VideoSettings g_video_settings_snapshot;
        g_video_settings_snapshot.enable_frame_pacer = value; });

    centerRegionToggle->init(brls::getStr("moonlight/settings_tab/center_region_title"), videoSettings.center_region_only, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.center_region_only = value;
        config.setVideoSettings(settings);
        config.save(); });

    showFpsToggle->init(brls::getStr("moonlight/settings_tab/show_fps_title"), videoSettings.show_fps, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.show_fps = value;
        config.setVideoSettings(settings);
        config.save();
        // Update global snapshot for immediate changes
        extern VideoSettings g_video_settings_snapshot;
        g_video_settings_snapshot.show_fps = value; });

    debugLogToggle->init(brls::getStr("moonlight/settings_tab/save_debug_log_title"), videoSettings.save_debug_log, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.save_debug_log = value;
        config.setVideoSettings(settings);
        config.save();
        // Update global flag for debug logs
        extern bool g_debug_log_enabled;
        g_debug_log_enabled = value;
        enable_file_logging(value); });

    refFrameInvalidationToggle->init(brls::getStr("moonlight/settings_tab/ref_frame_title"), videoSettings.enable_ref_frame_invalidation, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.enable_ref_frame_invalidation = value;
        config.setVideoSettings(settings);
        config.save(); });

    if (gyroSettingsEntry)
    {
        gyroSettingsEntry->setDetailText(brls::getStr("moonlight/gyro/settings_detail"));
        gyroSettingsEntry->registerClickAction([](brls::View*)
            {
            auto* gyroView = new GyroSettingsTab();
            auto* frame = new brls::AppletFrame(gyroView);
            frame->setTitle(brls::getStr("moonlight/gyro/title"));
            auto* act = new brls::Activity(frame);
            brls::Application::pushActivity(act);
            return true; });
    }

    doubleTapSprintToggle->init(brls::getStr("moonlight/settings_tab/double_tap_sprint_title"), videoSettings.enable_double_tap_sprint, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.enable_double_tap_sprint = value;
        config.setVideoSettings(settings);
        config.save(); });

    if (rearTouchSettingsEntry)
    {
        rearTouchSettingsEntry->setDetailText(brls::getStr("moonlight/settings/rear_touch_detail"));
        rearTouchSettingsEntry->registerClickAction([](brls::View*)
            {
            auto* rearTouchView = new RearTouchSettingsTab();
            // Prefer wrapping the settings view in an AppletFrame so the
            // standard header / footer are displayed (like in main.xml).
            // Set the title on the frame so the top header is visible.
            auto* frame = new brls::AppletFrame(rearTouchView);
            frame->setTitle(brls::getStr("moonlight/rear_touch/title"));
            auto* act = new brls::Activity(frame);
            brls::Application::pushActivity(act);
            return true; });
    }

    if (frontTouchSettingsEntry)
    {
        frontTouchSettingsEntry->setDetailText(brls::getStr("moonlight/settings/front_touch_detail"));
        frontTouchSettingsEntry->registerClickAction([](brls::View*)
            {
            auto* frontTouchView = new FrontTouchSettingsTab();
            auto* frame = new brls::AppletFrame(frontTouchView);
            frame->setTitle(brls::getStr("moonlight/front_touch/title"));
            auto* act = new brls::Activity(frame);
            brls::Application::pushActivity(act);
            return true; });
    }

    // Trackpad Settings Entry
    if (trackpadSettingsEntry)
    {
        trackpadSettingsEntry->setDetailText(brls::getStr("moonlight/trackpad/header"));
        trackpadSettingsEntry->registerClickAction([](brls::View*)
            {
            auto* trackpadView = new TrackpadSettingsTab();
            // Prefer wrapping the settings view in an AppletFrame so the
            // standard header / footer are displayed (like in main.xml).
            // Set the title on the frame so the top header is visible.
            auto* frame = new brls::AppletFrame(trackpadView);
            frame->setTitle(brls::getStr("moonlight/trackpad/header"));
            auto* act = new brls::Activity(frame);
            brls::Application::pushActivity(act);
            return true; });
    }

    // Configure touchscreen mode selector
    std::vector<std::string> touchscreenModes = {
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/0"),
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/1"),
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/2"),
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/3"),
        brls::getStr("moonlight/settings_tab/touchscreen_mode/options/4")
    };
    touchscreenModeSelector->init(brls::getStr("moonlight/settings_tab/touchscreen_mode/title"), touchscreenModes, videoSettings.touchscreen_mode, [this, touchscreenModes](int selected)
        {
        // Change touch mode at runtime (same as gamepad type)
        if (g_controllerInput && g_controllerInput->setTouchscreenModeRuntime(selected)) {
            // Success: Show notification with mode selected
            std::string message = brls::getStr("moonlight/settings_tab/touchscreen_mode/changed") +
                                  " " + touchscreenModes.at(selected);
            brls::Application::notify(message);
        } else {
            // Error: show incompatibility notification
            if (selected == 2) { // TOUCHSCREEN_MODE_DS4_TOUCHPAD
                brls::Application::notify("⚠ DS4 Touchpad solo compatible con PlayStation");
            } else {
                brls::Application::notify("⚠ No se pudo cambiar el modo táctil");
            }
        } });

    // Configure gamepad type selector (Xbox vs PS4)
    std::vector<std::string> gamepadTypes = {
        brls::getStr("moonlight/settings_tab/gamepad_type/options/xbox"),
        brls::getStr("moonlight/settings_tab/gamepad_type/options/ps4")
    };
    gamepadTypeSelector->init(brls::getStr("moonlight/settings_tab/gamepad_type/title"), gamepadTypes, (int)videoSettings.gamepad_type, [this, gamepadTypes](int selected)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        GamepadType newType = (selected == 0) ? GAMEPAD_TYPE_XBOX : GAMEPAD_TYPE_PS4;
        settings.gamepad_type = newType;
        config.setVideoSettings(settings);
        config.save();

        // Change gamepad type live without restarting session
        if (g_controllerInput) {
            g_controllerInput->setGamepadType(newType);
            std::string message = brls::getStr("moonlight/settings_tab/gamepad_type/notify_prefix") +
                                  gamepadTypes.at(selected);
            brls::Application::notify(message);
        } });

    // Configure swap shoulder buttons toggle
    swapShoulderButtonsToggle->init(brls::getStr("moonlight/settings_tab/swap_shoulder_buttons/title"), videoSettings.swap_shoulder_buttons, [this](bool value)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        settings.swap_shoulder_buttons = value;
        config.setVideoSettings(settings);
        config.save();
        if (g_controllerInput) {
            g_controllerInput->setSwapShoulderButtons(value);
        }
        brls::Application::notify(brls::getStr(value
            ? "moonlight/settings_tab/swap_shoulder_buttons/notify_enabled"
            : "moonlight/settings_tab/swap_shoulder_buttons/notify_disabled")); });

    // NUEVO: Configure Keyboard button
    if (keyboardConfigureCell)
    {
        keyboardConfigureCell->setDetailText(brls::getStr("moonlight/keyboard/configure_detail"));
        keyboardConfigureCell->registerClickAction([](brls::View*)
            {
            auto* keyboardView = new KeyboardSettingsTab();
            auto* frame = new brls::AppletFrame(keyboardView);
            frame->setTitle(brls::getStr("moonlight/keyboard/title"));
            auto* act = new brls::Activity(frame);
            brls::Application::pushActivity(act);
            return true; });
    }

    if (shortcutsConfigureCell)
    {
        shortcutsConfigureCell->setDetailText(brls::getStr("moonlight/shortcuts/configure_detail"));
        shortcutsConfigureCell->registerClickAction([](brls::View*)
            {
            auto* shortcutsView = new ShortcutsSettingsTab();
            auto* frame = new brls::AppletFrame(shortcutsView);
            frame->setTitle(brls::getStr("moonlight/shortcuts/title"));
            auto* act = new brls::Activity(frame);
            brls::Application::pushActivity(act);
            return true; });
    }

    // Enable Microphone toggle
    microphoneToggle->init(
        brls::getStr("moonlight/settings_tab/microphone_enabled_title"),
        videoSettings.enable_microphone,
        [this](bool value)
        {
            ConfigManager config;
            config.load();
            VideoSettings settings     = config.getVideoSettings();
            settings.enable_microphone = value;
            config.setVideoSettings(settings);
            config.save();

            // Note: Microphone will auto-start when streaming session begins
            // if enable_microphone is true

            brls::Application::notify(
                value ? brls::getStr("moonlight/settings_tab/microphone_enabled")
                      : brls::getStr("moonlight/settings_tab/microphone_disabled"));

            return true; // Return true to indicate successful save
        });

    // Configure Microphone button (opens dedicated settings view)
    microphoneConfigureCell->registerClickAction([](brls::View* view)
        {
        auto* micView = new MicrophoneSettingsTab();
        // Wrap in AppletFrame to show standard header/footer (like rear touch settings)
        auto* frame = new brls::AppletFrame(micView);
        frame->setTitle(brls::getStr("moonlight/microphone/title"));
        auto* act = new brls::Activity(frame);
        brls::Application::pushActivity(act);
        return true; });

    // Configure system options (original)
    radio->title->setText("Radio cell");
    radio->setSelected(radioSelected);
    radio->registerClickAction([this](brls::View* view)
        {
        radioSelected = !radioSelected;
        this->radio->setSelected(radioSelected);
        return true; });

    boolean->title->setText("Switcher");

    debug->init("Debug Layer", brls::Application::isDebuggingViewEnabled(), [](bool value)
        {
        brls::Application::enableDebuggingView(value);
        brls::sync([value](){
            vita_log::info("%s the debug layer", value ? "Open" : "Close");
        }); });

    bottomBar->init("Bottom Bar", !brls::AppletFrame::HIDE_BOTTOM_BAR, [](bool value)
        {
        brls::AppletFrame::HIDE_BOTTOM_BAR = !value;
        auto stack = brls::Application::getActivitiesStack();
        for (auto& activity : stack) {
            auto* frame = dynamic_cast<brls::AppletFrame*>(
                activity->getContentView());
            if (!frame) continue;
            frame->setFooterVisibility(!value ? brls::Visibility::GONE
                                             : brls::Visibility::VISIBLE);
        } });

    fps->init("FPS", brls::Application::getFPSStatus(), [](bool value)
        { brls::Application::setFPSStatus(value); });

    alwaysOnTop->init("Always On Top", false, [](bool value)
        { brls::Application::getPlatform()->setWindowAlwaysOnTop(value); });

    selector->init("Selector", { "Test 1", "Test 2", "Test 3", "Test 4", "Test 5", "Test 6", "Test 7", "Test 8", "Test 9", "Test 10", "Test 11", "Test 12", "Test 13" }, 0, [](int selected) {}, [](int selected)
        {
        auto dialog = new brls::Dialog(fmt::format("selected {}", selected));
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open(); });

    input->init(
        "Input text", "https://github.com", [](std::string text) {

        },
        "Placeholder", "Hint");

    inputNumeric->init(
        "Input number", 2448, [](int number) {

        },
        "Hint");

    // Async Init for system stats (IP, DNS, Brightness) to avoid lag
    ipAddress->setDetailText("...");
    dnsServer->setDetailText("...");

    input->registerAction("hints/open"_i18n, brls::BUTTON_X, [](brls::View* view)
        {
        brls::DetailCell *cell = dynamic_cast<brls::DetailCell *>(view);
        brls::Application::getPlatform()->openBrowser(cell->detail->getFullText());
        return true; }, false, false, brls::SOUND_CLICK);

    // Initial dummy value, updated async
    slider->init("Brightness", 0.0f, [this](float value)
        {
        brls::Application::getPlatform()->setBacklightBrightness(value);
        slider->setDetailText(fmt::format("{:.2f}", value)); });
    slider->setDetailText("...");

    // Configure Swap Interval (V-Sync)
    std::vector<std::string> swapIntervalOptions = {
        brls::getStr("moonlight/settings_tab/swap_interval/options/0"),
        brls::getStr("moonlight/settings_tab/swap_interval/options/1"),
        brls::getStr("moonlight/settings_tab/swap_interval/options/2"),
        brls::getStr("moonlight/settings_tab/swap_interval/options/3"),
        brls::getStr("moonlight/settings_tab/swap_interval/options/4")
    };
    int currentSwapInterval = videoSettings.swap_interval;
    if (currentSwapInterval < 0 || currentSwapInterval > 4)
        currentSwapInterval = 1;
    swapInterval->init(brls::getStr("moonlight/settings_tab/swap_interval/title"), swapIntervalOptions, currentSwapInterval, [](int selected)
        {
        if (selected >= 0 && selected <= 4) {
            ConfigManager config;
            config.load();
            VideoSettings settings = config.getVideoSettings();
            settings.swap_interval = selected;
            config.setVideoSettings(settings);
            config.save();

            extern VideoSettings g_video_settings_snapshot;
            g_video_settings_snapshot.swap_interval = selected;

            brls::Application::setSwapInterval(selected);
            brls::Application::notify(brls::getStr("moonlight/settings_tab/swap_interval/saved"));
        } });

    // Configure Buffer Mode
    std::vector<std::string> bufferModeOptions = {
        brls::getStr("moonlight/settings_tab/buffer_mode/options/0"),
        brls::getStr("moonlight/settings_tab/buffer_mode/options/1"),
        brls::getStr("moonlight/settings_tab/buffer_mode/options/2")
    };
    int currentBufferMode = videoSettings.buffer_mode;
    if (currentBufferMode < 0 || currentBufferMode > 2)
        currentBufferMode = 0;
    bufferModeSelector->init(brls::getStr("moonlight/settings_tab/buffer_mode/title"), bufferModeOptions, currentBufferMode, [](int selected)
        {
        if (selected >= 0 && selected <= 2) {
            ConfigManager config;
            config.load();
            VideoSettings settings = config.getVideoSettings();
            settings.buffer_mode = selected;
            config.setVideoSettings(settings);
            config.save();

            extern VideoSettings g_video_settings_snapshot;
            g_video_settings_snapshot.buffer_mode = selected;

            brls::Application::notify(brls::getStr("moonlight/settings_tab/buffer_mode/saved"));
        } });

    // Start background loading
    this->initAsync();

    notify->registerClickAction([](...)
        {
        std::string notification = NOTIFICATIONS[std::rand() % NOTIFICATIONS.size()];
        brls::Application::notify(notification);
        return true; });

    // Available languages
    std::vector<std::string> languages = {
        brls::getStr("moonlight/settings_tab/languages/es"),
        brls::getStr("moonlight/settings_tab/languages/en")
    };
    int currentLang           = 1; // 0: is, 1: en-US
    std::string currentLocale = brls::Application::getLocale();
    if (currentLocale == "es" || currentLocale == "es-ES")
        currentLang = 0;
    languageSelector->init(brls::getStr("moonlight/settings/language"), languages, currentLang, [this](int selected)
        {
        std::string locale = (selected == 0) ? "es" : "en-US";
#ifdef _WIN32
        _putenv_s("LANG", locale.c_str());
        _putenv_s("BOREALIS_LANG", locale.c_str());
#else
        setenv("LANG", locale.c_str(), 1);
        setenv("BOREALIS_LANG", locale.c_str(), 1);
#endif

        // Reload translations
        brls::loadTranslations();

        // Create directory if it does not exist
        std::string configPath = ConfigManager::getConfigPath();
        size_t pos = configPath.find_last_of("/\\");
        if (pos != std::string::npos) {
            std::string configDir = configPath.substr(0, pos);
#ifdef _WIN32
            CreateDirectoryA(configDir.c_str(), NULL);
#else
            mkdir(configDir.c_str(), 0755);
#endif
        }

        // Save the selected language directly
        ConfigManager config;
        config.set("general", "language", locale);
        config.save();

        brls::Application::notify(brls::getStr("moonlight/settings_tab/language_changed")); });
    SettingsTab::languageSelectorPtr = languageSelector;
}

brls::View* SettingsTab::create()
{
    return new SettingsTab();
}

void SettingsTab::initAsync()
{
    std::shared_ptr<bool> token = this->aliveToken;

    std::thread([this, token]()
        {
        // Fetch system info in background (may block on network)
        std::string ip = brls::Application::getPlatform()->getIpAddress();
        std::string dns = brls::Application::getPlatform()->getDnsServer();
        float brightness = brls::Application::getPlatform()->getBacklightBrightness();

        brls::sync([this, token, ip, dns, brightness]() {
            if (!(*token)) return;

            // Update UI
            ipAddress->setDetailText(ip);
            dnsServer->setDetailText(dns);

            // Update slider (re-init to set value correctly)
            slider->init("Brightness", brightness, [this](float value){
                brls::Application::getPlatform()->setBacklightBrightness(value);
                slider->setDetailText(fmt::format("{:.2f}", value));
            });
            slider->setDetailText(fmt::format("{:.2f}", brightness));
        }); })
        .detach();
}
