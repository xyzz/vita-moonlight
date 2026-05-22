#include "session/overlay/test_overlay_stream.hpp"

#include <borealis.hpp>

#include "debug.hpp"

TestOverlayStream::TestOverlayStream()
{
    this->setFocusable(true);
    // Button labels (in the same visual order as the pause overlay)
    buttonLabels = { "Resume", "Settings", "Disconnect", "Close App" };

    // Record navigation by turn signals and actions (A=confirm, B=cancel)
    this->registerAction("nav_up", brls::BUTTON_NAV_UP, [this](brls::View*)
        { this->moveFocus(-1); return true; });
    this->registerAction("nav_down", brls::BUTTON_NAV_DOWN, [this](brls::View*)
        { this->moveFocus(+1); return true; });
    this->registerAction("confirm", brls::BUTTON_A, [this](brls::View*)
        { this->activateFocused(); return true; });
    this->registerAction("cancel", brls::BUTTON_B, [this](brls::View*)
        { vita_log::info("[TestOverlayStream] cancel pressed"); return true; });

    // Also allow START to simulate resume/close (useful in testing)
    this->registerAction("start", brls::BUTTON_START, [this](brls::View*)
        { vita_log::info("[TestOverlayStream] START pressed"); return true; });
}

void TestOverlayStream::onLayout()
{
    // Nothing specific for now
}

void TestOverlayStream::moveFocus(int delta)
{
    int n = (int)buttonLabels.size();
    if (n == 0)
        return;
    focusedIndex = (focusedIndex + delta) % n;
    if (focusedIndex < 0)
        focusedIndex += n;
    // Forzar redraw
    this->invalidate();
}

void TestOverlayStream::activateFocused()
{
    vita_log::info("[TestOverlayStream] activateFocused index=%d label=%s", focusedIndex, buttonLabels[focusedIndex].c_str());
    // Here we could call real callbacks; for now just log
}

void TestOverlayStream::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    // Draw panel in upper left corner (480x544)
    float panelW = 480.0f;
    float panelH = 544.0f;
    float panelX = 0.0f;
    float panelY = 0.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, panelX, panelY, panelW, panelH, 8.0f);
    nvgFillColor(vg, nvgRGBA(18, 20, 24, 255));
    nvgFill(vg);

    // Header
    nvgFontSize(vg, 28.0f);
    nvgFontFaceId(vg, 0);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgText(vg, panelX + 28, panelY + 50, "Test Pause Overlay", 0);

    // Botones
    const float btnW      = 424.0f;
    const float btnH      = 56.0f;
    const float btnX      = panelX + 28.0f;
    float btnY            = panelY + 80.0f;
    const float btnMargin = 12.0f;

    nvgFontSize(vg, 22.0f);
    for (size_t i = 0; i < buttonLabels.size(); ++i)
    {
        const auto& label = buttonLabels[i];
        bool focused      = ((int)i == focusedIndex);

        // Fondo
        nvgBeginPath(vg);
        nvgRoundedRect(vg, btnX, btnY, btnW, btnH, 10.0f);
        if (focused)
            nvgFillColor(vg, nvgRGBA(80, 160, 255, 230));
        else
            nvgFillColor(vg, nvgRGBA(64, 64, 64, 220));
        nvgFill(vg);

        // Borde
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, focused ? 255 : 120));
        nvgStrokeWidth(vg, focused ? 3.0f : 1.5f);
        nvgStroke(vg);

        // Texto
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgText(vg, btnX + 16, btnY + 36, label.c_str(), 0);

        btnY += btnH + btnMargin;
    }
}
