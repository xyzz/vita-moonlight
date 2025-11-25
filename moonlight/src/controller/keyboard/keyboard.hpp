#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "utils/overlay_utils.hpp"

// Forward declare NVGcontext for draw override
struct NVGcontext;

class KeyboardOverlay : public BaseOverlay
{
  public:
    KeyboardOverlay(const std::string& cssPath);
    ~KeyboardOverlay() override;

    // Cargar CSS (parser simple) y aplicar propiedades
    bool loadCss(const std::string& path);

    // Mostrar/ocultar teclado (la integración con Borealis la hace quien empuje la vista)
    void show();
    void hide();

    // Parse result exposure for debugging
    std::unordered_map<std::string, std::string> getProperties() const { return properties; }

    void willAppear(bool resetState = false) override;

    // Dibujar custom para teclado en grid
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

  private:
    // Layout de teclas (filas de labels)
    std::vector<std::vector<std::string>> keyRows;
    void initDefaultLayout();
    void sendKeyByLabel(const std::string& label);

    std::string cssPath;
    std::unordered_map<std::string, std::string> properties;
    bool loaded = false;
    // Copia local del alpha del panel (BaseOverlay::panelAlpha es privado)
    float localPanelAlpha = 1.0f;
    // Estado de Shift (mayúsculas) — toggle cuando se pulsa la tecla Shift
    bool shiftActive = false;
};
