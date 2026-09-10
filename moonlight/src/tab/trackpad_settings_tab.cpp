/*
    SPDX-License-Identifier: GPL-3.0-only
*/

#include "tab/trackpad_settings_tab.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

#include "ConfigManager.hpp"
#include "controller/TouchInput.hpp"
#include "debug.hpp"

using namespace brls::literals;

TrackpadSettingsTab::TrackpadSettingsTab()
{
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/trackpad_settings.xml");

    ConfigManager config;
    config.load();
    VideoSettings videoSettings = config.getVideoSettings();

    // Range: 0-200, default 100 (0.0x - 2.0x multiplier)
    float initialPointerSpeed = static_cast<float>(videoSettings.trackpad_pointer_speed) / 200.0f;
    pointerSpeedSlider->init(brls::getStr("moonlight/trackpad/pointer_speed/title"), initialPointerSpeed, [this](float progress)
        {
        ConfigManager config;
        int speed = static_cast<int>(std::round(progress * 200.0f));
        speed = std::max(0, std::min(speed, 200));
        config.updateVideoSettings([speed](VideoSettings& settings) { settings.trackpad_pointer_speed = speed; });
        pointerSpeedSlider->setDetailText(fmt::format("{:.1f}x", speed / 100.0f));
        applyTrackpadSettingsLive(); });
    pointerSpeedSlider->setDetailText(fmt::format("{:.1f}x", videoSettings.trackpad_pointer_speed / 100.0f));

    // Enable right-click with two fingers
    twoFingerRightClickToggle->init(brls::getStr("moonlight/trackpad/two_finger_right_click/title"), videoSettings.trackpad_two_finger_right_click, [this](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.trackpad_two_finger_right_click = value; });
        brls::Application::notify(value ?
            brls::getStr("moonlight/trackpad/two_finger_right_click/enabled") :
            brls::getStr("moonlight/trackpad/two_finger_right_click/disabled"));
        applyTrackpadSettingsLive(); });

    // Enable scrolling with two fingers
    twoFingerScrollToggle->init(brls::getStr("moonlight/trackpad/two_finger_scroll/title"), videoSettings.trackpad_two_finger_scroll, [this](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.trackpad_two_finger_scroll = value; });
        brls::Application::notify(value ?
            brls::getStr("moonlight/trackpad/two_finger_scroll/enabled") :
            brls::getStr("moonlight/trackpad/two_finger_scroll/disabled"));
        applyTrackpadSettingsLive(); });

    // Natural scrolling vs traditional scrolling
    invertScrollToggle->init(brls::getStr("moonlight/trackpad/invert_scroll/title"), videoSettings.trackpad_invert_scroll, [this](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.trackpad_invert_scroll = value; });
        brls::Application::notify(value ?
            brls::getStr("moonlight/trackpad/invert_scroll/natural") :
            brls::getStr("moonlight/trackpad/invert_scroll/traditional"));
        applyTrackpadSettingsLive(); });

    // Enable advanced gestures (swipe 3 fingers, pinch to zoom, etc.)
    multiTouchGesturesToggle->init(brls::getStr("moonlight/trackpad/multi_touch/title"), videoSettings.trackpad_multi_touch, [this](bool value)
        {
        ConfigManager config;
        config.updateVideoSettings([value](VideoSettings& settings) { settings.trackpad_multi_touch = value; });
        brls::Application::notify(value ?
            brls::getStr("moonlight/trackpad/multi_touch/enabled") :
            brls::getStr("moonlight/trackpad/multi_touch/disabled"));
        applyTrackpadSettingsLive(); });

    // Define dead zones near the edges
    float initialEdgeZone = static_cast<float>(videoSettings.trackpad_edge_zone) / 50.0f;
    edgeZoneSlider->init(brls::getStr("moonlight/trackpad/edge_zone/title"), initialEdgeZone, [this](float progress)
        {
        ConfigManager config;
        int edgePercentage = static_cast<int>(roundf(progress * 50)); // Max 50%
        config.updateVideoSettings([edgePercentage](VideoSettings& settings) { settings.trackpad_edge_zone = edgePercentage; });
        edgeZoneSlider->setDetailText(std::to_string(edgePercentage) + "%");
        applyTrackpadSettingsLive(); });
    edgeZoneSlider->setDetailText(std::to_string(videoSettings.trackpad_edge_zone) + "%");

    // Minimum movement before registering as a swipe
    float initialDeadZone = static_cast<float>(videoSettings.trackpad_dead_zone) / 200.0f;
    deadZoneSlider->init(brls::getStr("moonlight/trackpad/dead_zone/title"), initialDeadZone, [this](float progress)
        {
        ConfigManager config;
        int deadZonePixels = static_cast<int>(roundf(progress * 200)); // Max 200px
        config.updateVideoSettings([deadZonePixels](VideoSettings& settings) { settings.trackpad_dead_zone = deadZonePixels; });
        deadZoneSlider->setDetailText(std::to_string(deadZonePixels) + "px");
        applyTrackpadSettingsLive(); });
    deadZoneSlider->setDetailText(std::to_string(videoSettings.trackpad_dead_zone) + "px");
}

// Auxiliary function to apply instant trackpad changes
void TrackpadSettingsTab::applyTrackpadSettingsLive()
{
    if (!g_touchInput)
        return;

    ConfigManager config;
    config.load();
    VideoSettings settings = config.getVideoSettings();

    g_touchInput->setTrackpadSettings(
        settings.trackpad_pointer_speed,
        settings.trackpad_dead_zone,
        true,
        settings.trackpad_two_finger_right_click,
        settings.trackpad_two_finger_scroll,
        settings.trackpad_invert_scroll,
        settings.trackpad_multi_touch,
        settings.trackpad_edge_zone);

    // Clear trackpad state to ensure it uses the new cache in the next frame
    g_touchInput->dropTouch(TOUCHSCREEN_MODE_TRACKPAD);

    vita_log::info(
        "[TRACKPAD] Settings applied live (pointer=%d deadzone=%d scroll=%d invert=%d)",
        settings.trackpad_pointer_speed,
        settings.trackpad_dead_zone,
        settings.trackpad_two_finger_scroll ? 1 : 0,
        settings.trackpad_invert_scroll ? 1 : 0);
}

brls::View* TrackpadSettingsTab::create()
{
    return new TrackpadSettingsTab();
}
