#include "session/overlay/ingame_overlay_view.hpp"

#include <borealis/views/button.hpp>
#include <borealis/views/label.hpp>

#include "borealis.hpp"

IngameOverlayView::IngameOverlayView(VitaSession* session)
    : m_session(session)
{
    // TabFrame has no internal title like AppletFrame; we can show an initial notify if desired
    // Add tabs using lambdas
    this->addTab("Sesión", [this]() -> brls::View*
        {
        auto box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
    m_statusLabel = new brls::Label();
    m_statusLabel->setText("Estado: ");
        updateStatus();
        box->addView(m_statusLabel);
        auto disconnect = new brls::Button();
        disconnect->setText("Desconectar");
        disconnect->registerClickAction([this](brls::View*) {
            if (m_session) m_session->stop(true);
            brls::Application::notify("Sesión terminada");
            brls::Application::popActivity(); // overlay
            brls::Application::popActivity(); // session view
            return true;
        });
        box->addView(disconnect);
        auto retry = new brls::Button();
        retry->setText("Reintentar reconexión");
        retry->registerClickAction([this](brls::View*) {
            if (m_session) {
                if (!m_session->attemptReconnect())
                    brls::Application::notify("Sin más intentos disponibles");
                else
                    brls::Application::notify("Intento de reconexión lanzado");
            }
            updateStatus();
            return true;
        });
        box->addView(retry);
        return box; });
    this->addTab("Opciones", [this]() -> brls::View*
        {
        auto box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
    auto bitrate = new brls::Label();
    bitrate->setText("Bitrate actual (placeholder) - No editable aún");
        box->addView(bitrate);
        return box; });
    // Action close with START
    this->registerAction("Cerrar", brls::ControllerButton::BUTTON_START, [this](brls::View*)
        {
        brls::Application::popActivity();
        return true; });
}

void IngameOverlayView::buildLogoutTab() { }
void IngameOverlayView::buildOptionsTab() { }

void IngameOverlayView::updateStatus()
{
    if (!m_statusLabel || !m_session)
        return;
    std::string txt = "Estado: ";
    if (m_session->isActive())
        txt += "Activo";
    else if (m_session->isTerminated())
        txt += "Terminado";
    else
        txt += "Iniciando";
    m_statusLabel->setText(txt);
}
