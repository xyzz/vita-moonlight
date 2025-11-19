#pragma once
#include <borealis/views/tab_frame.hpp>

#include "borealis.hpp"
#include "session/vita_session.hpp"

// Overlay ingame inspirado en Moonlight-Switch pero simplificado para Vita.
// Usa pestañas (Logout / Opciones) con posibilidad de expansión futura.

class IngameOverlayView : public brls::TabFrame
{
  public:
    IngameOverlayView(VitaSession* session);

  private:
    void buildLogoutTab();
    void buildOptionsTab();

    VitaSession* m_session;

    // Controles dinámicos básicos
    brls::Label* m_statusLabel = nullptr;

    void updateStatus();
};
