#include "tab/gyro_settings_tab.hpp"

#include <fmt/format.h>

#include <cmath>

#include "ConfigManager.hpp"
#include "controller/ControllerInput.hpp"
#include "view/gyro_test_overlay.hpp"

static constexpr float SLIDER_MIN = 0.1f;
static constexpr float SLIDER_MAX = 3.0f;

static float sliderToValue(float progress)
{
    float v = SLIDER_MIN + progress * (SLIDER_MAX - SLIDER_MIN);
    return std::round(v * 10.0f) / 10.0f; // 1 decimal
}

static float valueToSlider(float value)
{
    return (value - SLIDER_MIN) / (SLIDER_MAX - SLIDER_MIN);
}

static std::string scalarText(const char* axis, float value)
{
    return fmt::format("{} {}: {:.1f}", axis, brls::getStr("moonlight/gyro/scalar_suffix"), value);
}

GyroSettingsTab::GyroSettingsTab()
{
    this->inflateFromXMLRes("xml/tabs/gyro_settings.xml");

    ConfigManager config;
    config.load();
    VideoSettings videoSettings = config.getVideoSettings();

    // Motion controls toggle
    motionToggle->init(
        brls::getStr("moonlight/gyro/toggle"),
        videoSettings.enable_motion_controls,
        [](bool value)
        {
            ConfigManager cfg;
            cfg.load();
            VideoSettings settings          = cfg.getVideoSettings();
            settings.enable_motion_controls = value;
            cfg.setVideoSettings(settings);
            cfg.save();
            brls::Application::notify(value
                    ? brls::getStr("moonlight/gyro/notify_enabled")
                    : brls::getStr("moonlight/gyro/notify_disabled"));
            return true;
        });

    // Scalar X slider
    scalarXSlider->setProgress(valueToSlider(videoSettings.motion_controls_scalar_x));
    scalarXLabel->setText(scalarText("X", videoSettings.motion_controls_scalar_x));
    scalarXSlider->getProgressEvent()->subscribe([this](float progress)
        {
        float v = sliderToValue(progress);
        ConfigManager cfg;
        cfg.load();
        VideoSettings settings = cfg.getVideoSettings();
        settings.motion_controls_scalar_x = v;
        cfg.setVideoSettings(settings);
        cfg.save();
        scalarXLabel->setText(scalarText("X", v)); });

    // Scalar Y slider
    scalarYSlider->setProgress(valueToSlider(videoSettings.motion_controls_scalar_y));
    scalarYLabel->setText(scalarText("Y", videoSettings.motion_controls_scalar_y));
    scalarYSlider->getProgressEvent()->subscribe([this](float progress)
        {
        float v = sliderToValue(progress);
        ConfigManager cfg;
        cfg.load();
        VideoSettings settings = cfg.getVideoSettings();
        settings.motion_controls_scalar_y = v;
        cfg.setVideoSettings(settings);
        cfg.save();
        scalarYLabel->setText(scalarText("Y", v)); });

    // Test / calibrate button
    testCell->setDetailText(brls::getStr("moonlight/gyro/test_detail"));
    testCell->registerClickAction([](brls::View*)
        {
        GyroManager* gyro = nullptr;
        if (g_controllerInput) {
            gyro = g_controllerInput->getGyroManager();
        }
        auto* overlay = new GyroTestOverlay(gyro);
        brls::Application::pushActivity(new brls::Activity(overlay));
        return true; });

    this->registerAction(brls::getStr("hints/back"), brls::BUTTON_B, [](brls::View*)
        {
        brls::Application::popActivity();
        return true; });
}

brls::View* GyroSettingsTab::create()
{
    return new GyroSettingsTab();
}
