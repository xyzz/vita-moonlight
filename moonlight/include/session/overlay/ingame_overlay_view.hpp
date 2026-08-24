#pragma once
#include <borealis/views/tab_frame.hpp>

#include "borealis.hpp"
#include "session/vita_session.hpp"

class IngameOverlayView : public brls::TabFrame
{
  public:
    IngameOverlayView(VitaSession* session);

  private:
    void buildLogoutTab();
    void buildOptionsTab();

    VitaSession* m_session;

    brls::Label* m_statusLabel = nullptr;

    void updateStatus();
};
