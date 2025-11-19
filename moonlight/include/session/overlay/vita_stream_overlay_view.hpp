#pragma once
#include <borealis.hpp>

#include "utils/overlay_utils.hpp"
#include "video/legacy/vita.hpp"

// Vista simple de overlay para mostrar estadísticas de video en Vita.
// En el futuro se puede expandir con controles (pausa, bitrate, etc.).
class VitaStreamOverlayView : public BaseOverlay
{
  public:
    VitaStreamOverlayView();
    ~VitaStreamOverlayView() override = default;

    // Implementación de interfaz View
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    void onLayout() override; // se usa en vez de layout(...)
    brls::View* getDefaultFocus() override { return nullptr; }
    const char* describe() const { return "VitaStreamOverlayView"; }

  private:
    uint64_t lastFetchMs = 0;
    VitaVideoStats cached; // se inicializa en constructor via vitavideo_get_stats
};
