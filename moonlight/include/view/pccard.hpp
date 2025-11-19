/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <borealis.hpp>

// Widget reutilizable para mostrar una "PC card" como un botón grande con imagen y nombre centrados.
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
    brls::Box* box     = nullptr; // Guarda el layout para poder actualizarlo si se requiere
    std::function<void()> clickCb;
};
