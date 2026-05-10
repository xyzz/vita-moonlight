#include "tab/front_touch_settings_tab.hpp"

#include <string>
#include <vector>

#include "ConfigManager.hpp"
#include "controller/ControllerInput.hpp"
#include "controller/special_inputs.hpp"
#include "view/front_touch_preview_overlay.hpp"

FrontTouchSettingsTab::FrontTouchSettingsTab()
{
    this->inflateFromXMLRes("xml/tabs/front_touch_settings.xml");

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

    auto updateSliderDetails = [this](const VideoSettings& settings)
    {
        if (frontTouchOffsetSlider)
        {
            frontTouchOffsetSlider->setDetailText(std::to_string(settings.front_touch_offset) + " px");
        }
        if (frontTouchSizeSlider)
        {
            frontTouchSizeSlider->setDetailText(std::to_string(settings.front_touch_size) + " px");
        }
    };

    auto applyFrontTouchActions = [this, updateSliderDetails](const VideoSettings& settings)
    {
        if (frontTouchNWSelector)
        {
            frontTouchNWSelector->setSelection(controller::getSelectableIndexForCode(settings.front_action_northwest), true);
        }
        if (frontTouchNESelector)
        {
            frontTouchNESelector->setSelection(controller::getSelectableIndexForCode(settings.front_action_northeast), true);
        }
        if (frontTouchSWSelector)
        {
            frontTouchSWSelector->setSelection(controller::getSelectableIndexForCode(settings.front_action_southwest), true);
        }
        if (frontTouchSESelector)
        {
            frontTouchSESelector->setSelection(controller::getSelectableIndexForCode(settings.front_action_southeast), true);
        }
        updateSliderDetails(settings);
    };

    frontTouchToggle->init(brls::getStr("moonlight/front_touch/toggle"), videoSettings.enable_front_touchzones,
        [this, applyFrontTouchActions](bool value)
        {
            ConfigManager config;
            config.load();
            VideoSettings settings           = config.getVideoSettings();
            settings.enable_front_touchzones = value;
            config.setVideoSettings(settings);
            config.save();
            if (g_controllerInput)
            {
                g_controllerInput->setFrontTouchEnabled(value);
                if (value)
                {
                    g_controllerInput->applyFrontTouchSettings(settings);
                }
            }
            applyFrontTouchActions(settings);
            brls::Application::notify(value ? brls::getStr("moonlight/front_touch/notify_enabled")
                                            : brls::getStr("moonlight/front_touch/notify_disabled"));
        });

    if (frontTouchOffsetSlider)
    {
        float offsetProgress = videoSettings.front_touch_offset / 200.0f;
        frontTouchOffsetSlider->init(brls::getStr("moonlight/front_touch/offset"), offsetProgress,
            [this, updateSliderDetails](float progress)
            {
                int offset = static_cast<int>(progress * 200.0f);
                ConfigManager config;
                config.load();
                VideoSettings settings      = config.getVideoSettings();
                settings.front_touch_offset = offset;
                config.setVideoSettings(settings);
                config.save();
                if (g_controllerInput)
                {
                    g_controllerInput->applyFrontTouchSettings(settings);
                }
                updateSliderDetails(settings);
            });
        updateSliderDetails(videoSettings);
    }

    if (frontTouchSizeSlider)
    {
        float sizeProgress = (videoSettings.front_touch_size - 50.0f) / 250.0f;
        frontTouchSizeSlider->init(brls::getStr("moonlight/front_touch/size"), sizeProgress,
            [this, updateSliderDetails](float progress)
            {
                int size = 50 + static_cast<int>(progress * 250.0f);
                ConfigManager config;
                config.load();
                VideoSettings settings    = config.getVideoSettings();
                settings.front_touch_size = size;
                config.setVideoSettings(settings);
                config.save();
                if (g_controllerInput)
                {
                    g_controllerInput->applyFrontTouchSettings(settings);
                }
                updateSliderDetails(settings);
            });
        updateSliderDetails(videoSettings);
    }

    if (frontTouchPreviewCell)
    {
        frontTouchPreviewCell->setDetailText(brls::getStr("moonlight/front_touch/preview_detail"));
        frontTouchPreviewCell->registerClickAction([this](brls::View*)
            {
            ConfigManager config;
            config.load();
            VideoSettings settings = config.getVideoSettings();
            auto* overlay = new FrontTouchPreviewOverlay(settings.front_touch_offset, settings.front_touch_size);
            brls::Application::pushActivity(new brls::Activity(overlay));
            return true; });
    }

    auto configureActionSelector = [specialOptionNames, this, applyFrontTouchActions](brls::SelectorCell* selector, std::uint32_t VideoSettings::* member)
    {
        if (!selector)
            return;

        ConfigManager config;
        config.load();
        VideoSettings videoSettings = config.getVideoSettings();

        std::string selectorTitle = selector->title ? selector->title->getFullText() : "";
        selector->init(selectorTitle, specialOptionNames,
            controller::getSelectableIndexForCode(videoSettings.*member),
            [member, applyFrontTouchActions](int selected)
            {
                ConfigManager config;
                config.load();
                VideoSettings settings = config.getVideoSettings();
                settings.*member       = controller::getCodeForSelectableIndex(static_cast<std::size_t>(selected));
                config.setVideoSettings(settings);
                config.save();
                if (g_controllerInput)
                {
                    g_controllerInput->applyFrontTouchSettings(settings);
                    g_controllerInput->setFrontTouchEnabled(settings.enable_front_touchzones);
                }
                applyFrontTouchActions(settings);
                brls::Application::notify(brls::getStr("moonlight/front_touch/notify_action_saved"));
            });
    };

    configureActionSelector(frontTouchNWSelector, &VideoSettings::front_action_northwest);
    configureActionSelector(frontTouchNESelector, &VideoSettings::front_action_northeast);
    configureActionSelector(frontTouchSWSelector, &VideoSettings::front_action_southwest);
    configureActionSelector(frontTouchSESelector, &VideoSettings::front_action_southeast);

    applyFrontTouchActions(videoSettings);

    this->registerAction(brls::getStr("hints/back"), brls::BUTTON_B, [](brls::View*)
        {
        brls::Application::popActivity();
        return true; });
}

brls::View* FrontTouchSettingsTab::create()
{
    return new FrontTouchSettingsTab();
}
