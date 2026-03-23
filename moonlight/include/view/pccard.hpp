/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <borealis.hpp>

// Reusable widget to display a "PC card" as a large button with centered image and name.
class PCCard : public brls::Button
{
  public:
    PCCard(const std::string& name, const std::string& imagePath = "resources/img/moonlight/pc.png");

    void setPCName(const std::string& name);
    void setPCImage(const std::string& imagePath);
    void setClickAction(std::function<void()> cb);

  private:
    brls::Image* image = nullptr;
    brls::Label* label = nullptr;
    brls::Box* box     = nullptr; // Save the layout so you can update it if required
    std::function<void()> clickCb;
};
