#pragma once
#include <borealis.hpp>
#include <string>
#include <vector>

// Opciones para personalizar diálogos
struct DialogOptions
{
    float contentPadding                = 0.0f; // Padding del contenido (0 = sin padding)
    float contentWidth                  = 0.0f; // Ancho del contenido (0 = auto)
    brls::AlignItems alignItems         = brls::AlignItems::AUTO;
    brls::JustifyContent justifyContent = brls::JustifyContent::FLEX_START;
    bool cancelable                     = true; // ¿Presionar B cierra el diálogo?
    bool focusable                      = true;
    bool hideHighlight                  = false;
};

brls::Dialog* createCustomDialog(brls::Box* content, const DialogOptions& options);

brls::Label* createLabel(const std::string& text, float fontSize = 20.0f,
    brls::HorizontalAlign align = brls::HorizontalAlign::LEFT,
    float marginBottom          = 0.0f);

brls::Box* createInfoBox(const std::vector<std::pair<std::string, std::string>>& rows,
    float fontSize = 20.0f, float rowSpacing = 10.0f);

// Funciones originales
brls::Dialog* createLoadingDialog(const std::string& message);
// Actualiza el texto del diálogo devuelto por createLoadingDialog. Devuelve true si se encontró
// y actualizó una etiqueta dentro del diálogo.
bool updateLoadingDialogText(brls::Dialog* dialog, const std::string& message);
