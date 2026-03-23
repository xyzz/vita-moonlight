#pragma once
#include <borealis/views/tab_frame.hpp>

#include "borealis.hpp"
#include "session/vita_session.hpp"

// Ingame overlay inspired by Moonlight-Switch but simplified for Vita.
// Uses tabs (Logout / Options) with the possibility of future expansion.

class IngameOverlayView : public brls::TabFrame
{
  public:
    IngameOverlayView(VitaSession* session);

  private:
    void buildLogoutTab();
    void buildOptionsTab();

    VitaSession* m_session;

    // Basic dynamic controls
    brls::Label* m_statusLabel = nullptr;

    void updateStatus();
};
