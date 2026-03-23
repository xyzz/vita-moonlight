#pragma once
#include <borealis.hpp>
#include <string>
#include <vector>

struct DialogOptions
{
    float contentPadding                = 0.0f; // Content padding (0 = no padding)
    float contentWidth                  = 0.0f; // Content width (0 = auto)
    brls::AlignItems alignItems         = brls::AlignItems::AUTO;
    brls::JustifyContent justifyContent = brls::JustifyContent::FLEX_START;
    bool cancelable                     = true; // Does pressing B close the dialog?
    bool focusable                      = true;
    bool hideHighlight                  = false;
};

brls::Dialog* createCustomDialog(brls::Box* content, const DialogOptions& options);

brls::Label* createLabel(const std::string& text, float fontSize = 20.0f,
    brls::HorizontalAlign align = brls::HorizontalAlign::LEFT,
    float marginBottom          = 0.0f);

brls::Box* createInfoBox(const std::vector<std::pair<std::string, std::string>>& rows,
    float fontSize = 20.0f, float rowSpacing = 10.0f);

brls::Dialog* createLoadingDialog(const std::string& message);
bool updateLoadingDialogText(brls::Dialog* dialog, const std::string& message);
