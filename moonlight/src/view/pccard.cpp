/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#include "view/pccard.hpp"

#include <borealis/core/application.hpp>

// Helper function to measure text width
float measureTextWidth(int font, float fontSize, const std::string& text)
{
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg)
    {
        // Safe Fallback: If no NVG context is available (can occur in stages
        // early initialization on Vita), we approximate the width with a simple rule.
        // This avoids crashes and returns a reasonable value for the truncation logic.
        float approxCharWidth = fontSize * 0.6f;
        return approxCharWidth * static_cast<float>(text.size());
    }
    nvgFontSize(vg, fontSize);
    nvgFontFaceId(vg, font);
    float bounds[4];
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    return bounds[2] - bounds[0];
}

// Visual card as large reusable button
PCCard::PCCard(const std::string& name, const std::string& imagePath)
    : brls::Button()
{
    this->setText("");
    this->setWidth(180);
    this->setHeight(140);
    this->setPadding(0);
    this->setCornerRadius(16);
    this->setBackgroundColor(nvgRGB(35, 39, 46));
    this->setBorderColor(nvgRGB(100, 100, 120));
    this->setBorderThickness(2);

    // Vertical layout: image and label

    box = new brls::Box(brls::Axis::COLUMN);
    box->setWidth(180);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setFocusable(false);

    this->setFocusable(true);

    image           = new brls::Image();
    bool isExternal = (imagePath.rfind("ux0:", 0) == 0 || imagePath.rfind("/", 0) == 0 || imagePath.rfind("cache/", 0) == 0);
    if (isExternal)
    {
        image->setImageFromFile(imagePath);
        image->setWidth(140);
        image->setHeight(80);
        image->setScalingType(brls::ImageScalingType::FIT);
        image->setMarginTop(10);
        image->setMarginBottom(10);
    }
    else
    {
        std::string fixedPath = (imagePath == "resources/img/moonlight/pc.png") ? "img/moonlight/pc.png" : imagePath;
        image->setImageFromRes(fixedPath);
        image->setWidth(48);
        image->setHeight(48);
        image->setMarginTop(20);
        image->setMarginBottom(18); // Extra separation between image and text
    }
    box->addView(image);

    label = new brls::Label();
    label->setText(name);
    label->setFontSize(16);
    label->setMargins(0, 0, 0, 0); // The upper margin is already given by the image
    label->setWidth(160); // A little less than the card for padding
    label->setTextColor(nvgRGB(255, 255, 255));
    label->setSingleLine(true); // Required for animation
    label->setAutoAnimate(false); // We disable auto-animation by focus
    // Manual calculation of text width to decide animation and alignment using new public API
    float textWidth = measureTextWidth(label->getFont(), label->getFontSize(), name);
    if (textWidth > 160)
    {
        label->setHorizontalAlign(brls::HorizontalAlign::LEFT);
        label->setAnimated(true); // Animate if truncated
    }
    else
    {
        label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        label->setAnimated(false);
    }
    box->addView(label);

    this->clearViews();
    this->addView(box);
    this->label = label;

    this->registerClickAction([this](brls::View*)
        {
        if (clickCb) clickCb();
        return true; });
}

void PCCard::setPCName(const std::string& name)
{
    if (label)
    {
        label->setText(name);
        if (name.empty())
        {
            if (image)
            {
                image->setVisibility(brls::Visibility::GONE);
            }
            // Disable animation if there is no text
            label->setAnimated(false);
            label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            return;
        }
        // Recalculate animation and alignment if you change name using new public API
        if (image)
            image->setVisibility(brls::Visibility::VISIBLE);
        float textWidth = measureTextWidth(label->getFont(), label->getFontSize(), name);
        if (textWidth > 160)
        {
            label->setHorizontalAlign(brls::HorizontalAlign::LEFT);
            label->setAnimated(true);
        }
        else
        {
            label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            label->setAnimated(false);
        }
    }
}

void PCCard::setPCImage(const std::string& imagePath)
{
    if (image)
    {
        if (imagePath.empty())
        {
            image->setVisibility(brls::Visibility::GONE);
            return;
        }
        bool isExternal = (imagePath.rfind("ux0:", 0) == 0 || imagePath.rfind("/", 0) == 0 || imagePath.rfind("cache/", 0) == 0);
        if (isExternal)
        {
            image->setImageFromFile(imagePath);
            image->setWidth(140);
            image->setHeight(80);
            image->setScalingType(brls::ImageScalingType::FIT);
            image->setMarginTop(10);
            image->setMarginBottom(10);
        }
        else
        {
            std::string fixedPath = (imagePath == "resources/img/moonlight/pc.png") ? "img/moonlight/pc.png" : imagePath;
            image->setImageFromRes(fixedPath);
            image->setWidth(48);
            image->setHeight(48);
            image->setMarginTop(20);
            image->setMarginBottom(18);
        }
        image->setVisibility(brls::Visibility::VISIBLE);
    }
}

void PCCard::setClickAction(std::function<void()> cb)
{
    clickCb = cb;
}
