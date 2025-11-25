// VitaVideoRenderer.hpp - Renderer simplificado centralizado (sin staging NVG)
#pragma once

#include <stdint.h>
#include <vita2d.h>
extern bool vita2d_inited; // definido en vita_globals.cpp
struct NVGcontext; // forward

// No dependemos de NanoVG para el render (solo vita2d)

class VitaVideoRenderer final
{
  public:
    static VitaVideoRenderer& instance();
    void draw(float viewportW, float viewportH);
    // Compatibilidad con llamada antigua que incluía NVGcontext/alpha
    void draw(struct NVGcontext* vg, float viewportW, float viewportH, float alpha = 1.0f);
    // Nueva ruta: dibujar usando NanoVG (evita begin/end duplicados de vita2d)
    void drawNVG(struct NVGcontext* vg, float viewportW, float viewportH, float alpha = 1.0f);
    void setFullscreenStretch(bool stretch);
    bool isFullscreenStretch() const { return fullscreenStretch; }
    // Release NVG image that references the current vita2d texture.
    // Public so UI code can clear any NVG references before stopping video.
    void destroyImage(struct NVGcontext* vg);

  private:
    VitaVideoRenderer()    = default;
    bool fullscreenStretch = true; // sincronizado con global video_fullscreen_stretch
    // Recursos para imagen NVG (textura GXM directa)
    struct NvgImageCacheEntry
    {
        const vita2d_texture* tex = nullptr;
        int imageId               = -1;
        int width                 = 0;
        int height                = 0;
    };

    // Cache simple (max 2-3 entradas usualmente)
    NvgImageCacheEntry imageCache[4];
    int imageCacheSize = 0;

    // Diagnósticos
    uint32_t nvgImageCreateCount = 0;
};
