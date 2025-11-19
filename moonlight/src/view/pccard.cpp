/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#include "view/pccard.hpp"

#include <borealis/core/application.hpp>

// Función helper para medir ancho de texto
float measureTextWidth(int font, float fontSize, const std::string& text)
{
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg)
    {
        // Fallback seguro: si no hay contexto NVG disponible (puede ocurrir en etapas
        // tempranas de inicialización en Vita), approximamos el ancho con una regla simple.
        // Esto evita crashes y devuelve un valor razonable para la lógica de truncado.
        float approxCharWidth = fontSize * 0.6f;
        return approxCharWidth * static_cast<float>(text.size());
    }
    nvgFontSize(vg, fontSize);
    nvgFontFaceId(vg, font);
    float bounds[4];
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    return bounds[2] - bounds[0];
}

// Card visual como botón grande reutilizable
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

    // Layout vertical: imagen y label

    box = new brls::Box(brls::Axis::COLUMN);
    box->setWidth(180);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setFocusable(false);

    // Make the card focusable so navigation selects the card (restore selection UX)
    this->setFocusable(true);

    image                 = new brls::Image();
    std::string fixedPath = (imagePath == "resources/img/moonlight/pc.png") ? "img/moonlight/pc.png" : imagePath;
    // Cargar la imagen inmediatamente (ruta fija ya calculada arriba)
    image->setImageFromRes(fixedPath);
    image->setWidth(48);
    image->setHeight(48);
    image->setMarginTop(20);
    image->setMarginBottom(18); // Separación extra entre imagen y texto
    box->addView(image);

    label = new brls::Label();
    label->setText(name);
    label->setFontSize(16);
    label->setMargins(0, 0, 0, 0); // El margen superior ya lo da la imagen
    label->setWidth(160); // Un poco menos que la card para padding
    label->setTextColor(nvgRGB(255, 255, 255));
    label->setSingleLine(true); // Necesario para animación
    label->setAutoAnimate(false); // Desactivamos auto-animación por foco
    // Cálculo manual del ancho del texto para decidir animación y alineación usando la nueva API pública
    float textWidth = measureTextWidth(label->getFont(), label->getFontSize(), name);
    if (textWidth > 160)
    {
        label->setHorizontalAlign(brls::HorizontalAlign::LEFT);
        label->setAnimated(true); // Animar si se trunca
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
            // Si no hay nombre, ocultamos imagen para evitar que quede una tarjeta con solo foto
            if (image)
            {
                image->setVisibility(brls::Visibility::GONE);
            }
            // Desactivar animación si no hay texto
            label->setAnimated(false);
            label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            return;
        }
        // Recalcular animación y alineación si cambia el nombre usando la nueva API pública
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
        std::string fixedPath = (imagePath == "resources/img/moonlight/pc.png") ? "img/moonlight/pc.png" : imagePath;
        image->setImageFromRes(fixedPath);
        image->setVisibility(brls::Visibility::VISIBLE);
    }
}

void PCCard::setClickAction(std::function<void()> cb)
{
    clickCb = cb;
}
