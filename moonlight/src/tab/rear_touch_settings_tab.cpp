#include "tab/rear_touch_settings_tab.hpp"

#include <string>
#include <vector>

#include "ConfigManager.hpp"
#include "controller/ControllerInput.hpp"
#include "controller/special_inputs.hpp"
#include "debug.hpp"
#include "view/rear_touch_calibration_overlay.hpp"

RearTouchSettingsTab::RearTouchSettingsTab()
{
    this->inflateFromXMLRes("xml/tabs/rear_touch_settings.xml");

    ConfigManager config;
    config.load();
    VideoSettings videoSettings = config.getVideoSettings();

    const auto& specialOptions = controller::getSelectableSpecialInputOptions();
    std::vector<std::string> specialOptionNames;
    specialOptionNames.reserve(specialOptions.size());
    for (const auto& option : specialOptions)
    {
        specialOptionNames.push_back(option.name);
    }

    auto updateRearTouchDetail = [this](const RearTouchSettings& rtSettings)
    {
        if (!rearTouchCalibrationCell)
            return;
        std::string detail = brls::getStr("moonlight/rear_touch/detail_top") + " " + std::to_string(rtSettings.top) + " | " + brls::getStr("moonlight/rear_touch/detail_bottom") + " " + std::to_string(rtSettings.bottom) + " | " + brls::getStr("moonlight/rear_touch/detail_left") + " " + std::to_string(rtSettings.left) + " | " + brls::getStr("moonlight/rear_touch/detail_right") + " " + std::to_string(rtSettings.right);
        rearTouchCalibrationCell->setDetailText(detail);
    };

    auto applyRearTouchActions = [this](const RearTouchSettings& settings)
    {
        if (rearTouchNWSelector)
        {
            rearTouchNWSelector->setSelection(controller::getSelectableIndexForCode(settings.actionNorthWest), true);
        }
        if (rearTouchNESelector)
        {
            rearTouchNESelector->setSelection(controller::getSelectableIndexForCode(settings.actionNorthEast), true);
        }
        if (rearTouchSWSelector)
        {
            rearTouchSWSelector->setSelection(controller::getSelectableIndexForCode(settings.actionSouthWest), true);
        }
        if (rearTouchSESelector)
        {
            rearTouchSESelector->setSelection(controller::getSelectableIndexForCode(settings.actionSouthEast), true);
        }
    };

    rearTouchToggle->init(brls::getStr("moonlight/rear_touch/toggle"), videoSettings.rear_touch.enabled,
        [this, updateRearTouchDetail, applyRearTouchActions](bool value)
        {
            ConfigManager config;
            config.load();
            VideoSettings settings      = config.getVideoSettings();
            settings.rear_touch.enabled = value;
            config.setVideoSettings(settings);
            config.save();
            if (g_controllerInput)
            {
                g_controllerInput->setRearTouchEnabled(value);
                if (value)
                {
                    g_controllerInput->applyRearTouchSettings(settings.rear_touch);
                }
            }
            updateRearTouchDetail(settings.rear_touch);
            applyRearTouchActions(settings.rear_touch);
            brls::Application::notify(value ? brls::getStr("moonlight/rear_touch/notify_enabled")
                                            : brls::getStr("moonlight/rear_touch/notify_disabled"));
        });

    auto configureActionSelector = [specialOptionNames, this, applyRearTouchActions](brls::SelectorCell* selector, std::uint32_t RearTouchSettings::* member)
    {
        if (!selector)
            return;

        ConfigManager config;
        VideoSettings videoSettings;
        config.load();
        videoSettings = config.getVideoSettings();

        std::string selectorTitle = selector->title ? selector->title->getFullText() : "";
        selector->init(selectorTitle, specialOptionNames,
            controller::getSelectableIndexForCode(videoSettings.rear_touch.*member),
            [member, applyRearTouchActions](int selected)
            {
                ConfigManager config;
                config.load();
                VideoSettings settings      = config.getVideoSettings();
                settings.rear_touch.*member = controller::getCodeForSelectableIndex(static_cast<std::size_t>(selected));
                config.setVideoSettings(settings);
                config.save();
                if (g_controllerInput)
                {
                    g_controllerInput->applyRearTouchSettings(settings.rear_touch);
                    g_controllerInput->setRearTouchEnabled(settings.rear_touch.enabled);
                }
                applyRearTouchActions(settings.rear_touch);
                brls::Application::notify(brls::getStr("moonlight/rear_touch/notify_action_saved"));
            });
    };

    configureActionSelector(rearTouchNWSelector, &RearTouchSettings::actionNorthWest);
    configureActionSelector(rearTouchNESelector, &RearTouchSettings::actionNorthEast);
    configureActionSelector(rearTouchSWSelector, &RearTouchSettings::actionSouthWest);
    configureActionSelector(rearTouchSESelector, &RearTouchSettings::actionSouthEast);

    applyRearTouchActions(videoSettings.rear_touch);
    updateRearTouchDetail(videoSettings.rear_touch);

    rearTouchCalibrationCell->registerClickAction([this, updateRearTouchDetail, applyRearTouchActions](brls::View*)
        {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        auto initial = settings.rear_touch;
        auto* overlay = new RearTouchCalibrationOverlay(initial,
            [this, updateRearTouchDetail, applyRearTouchActions](const RearTouchSettings& updated) {
                ConfigManager cfg;
                cfg.load();
                VideoSettings vs = cfg.getVideoSettings();
                vs.rear_touch = updated;
                cfg.setVideoSettings(vs);
                cfg.save();
                if (g_controllerInput) {
                    g_controllerInput->applyRearTouchSettings(updated);
                    g_controllerInput->setRearTouchEnabled(updated.enabled);
                }
                if (rearTouchToggle) {
                    rearTouchToggle->setOn(updated.enabled, false);
                }
                updateRearTouchDetail(updated);
                applyRearTouchActions(updated);
                brls::Application::notify(brls::getStr("moonlight/rear_touch/notify_calibration_saved"));
            },
            [this, updateRearTouchDetail]() {
                ConfigManager cfg;
                cfg.load();
                VideoSettings vs = cfg.getVideoSettings();
                updateRearTouchDetail(vs.rear_touch);
            }
        );
    vita_debug_log("[RearTouchSettingsTab] pushing RearTouchCalibrationOverlay - itemTitle='%s'", overlay->getAppletFrameItem()->title.c_str());
    brls::Application::pushActivity(new brls::Activity(overlay));
        return true; });

    this->registerAction(brls::getStr("hints/back"), brls::BUTTON_B, [](brls::View*)
        {
        brls::Application::popActivity();
        return true; });
}

brls::View* RearTouchSettingsTab::create()
{
    return new RearTouchSettingsTab();
}
