#include "view/front_touch_preview_overlay.hpp"

#include <string.h>

#include <algorithm>

namespace
{
constexpr float PANEL_WIDTH  = 960.0f;
constexpr float PANEL_HEIGHT = 544.0f;
constexpr float MAX_TOUCH_X  = 1919.0f;
constexpr float MAX_TOUCH_Y  = 1087.0f;

inline float clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}
}

FrontTouchPreviewCanvas::FrontTouchPreviewCanvas(int offset, int size)
    : offset(offset)
    , size(size)
{
    this->setFocusable(true);
    this->setWidth(PANEL_WIDTH);
    this->setHeight(PANEL_HEIGHT);
}

void FrontTouchPreviewCanvas::setOffset(int o)
{
    offset = o;
}

void FrontTouchPreviewCanvas::setSize(int s)
{
    size = s;
}

void FrontTouchPreviewCanvas::drawZone(NVGcontext* vg, float x, float y, float w, float h, const char* label, bool active)
{
    // Zone fill
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    if (active)
    {
        nvgFillColor(vg, nvgRGBA(0, 200, 100, 180));
    }
    else
    {
        nvgFillColor(vg, nvgRGBA(100, 100, 100, 120));
    }
    nvgFill(vg);

    // Zone border
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 200));
    nvgStrokeWidth(vg, 2.0f);
    nvgStroke(vg);

    // Label
    nvgFontFaceId(vg, 0);
    nvgFontSize(vg, 20.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 230));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + w * 0.5f, y + h * 0.5f, label, nullptr);
}

void FrontTouchPreviewCanvas::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    nvgSave(vg);

    // Dark background
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(20, 20, 20, 240));
    nvgFill(vg);

    // Scale to fit the Vita screen proportionally within available area
    float scale = std::min(width / PANEL_WIDTH, height / PANEL_HEIGHT);
    float drawW = PANEL_WIDTH * scale;
    float drawH = PANEL_HEIGHT * scale;
    float drawX = x + (width - drawW) * 0.5f;
    float drawY = y + (height - drawH) * 0.5f;

    // Screen outline
    nvgBeginPath(vg);
    nvgRect(vg, drawX, drawY, drawW, drawH);
    nvgStrokeColor(vg, nvgRGBA(200, 200, 200, 255));
    nvgStrokeWidth(vg, 3.0f);
    nvgStroke(vg);

    SceTouchData frontData;
    memset(&frontData, 0, sizeof(SceTouchData));
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &frontData, 1);

    bool zoneActive[4] = { false, false, false, false };
    for (int i = 0; i < frontData.reportNum; ++i)
    {
        float tx = clamp01(static_cast<float>(frontData.report[i].x) / MAX_TOUCH_X) * PANEL_WIDTH;
        float ty = clamp01(static_cast<float>(frontData.report[i].y) / MAX_TOUCH_Y) * PANEL_HEIGHT;

        // NW
        if (tx >= offset && tx <= offset + size && ty >= offset && ty <= offset + size)
            zoneActive[0] = true;
        // NE
        if (tx >= PANEL_WIDTH - offset - size && tx <= PANEL_WIDTH - offset && ty >= offset && ty <= offset + size)
            zoneActive[1] = true;
        // SW
        if (tx >= offset && tx <= offset + size && ty >= PANEL_HEIGHT - offset - size && ty <= PANEL_HEIGHT - offset)
            zoneActive[2] = true;
        // SE
        if (tx >= PANEL_WIDTH - offset - size && tx <= PANEL_WIDTH - offset && ty >= PANEL_HEIGHT - offset - size && ty <= PANEL_HEIGHT - offset)
            zoneActive[3] = true;

        nvgBeginPath(vg);
        nvgCircle(vg, drawX + tx * scale, drawY + ty * scale, 12.0f * scale);
        nvgFillColor(vg, nvgRGBA(255, 50, 50, 200));
        nvgFill(vg);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);
    }

    float so = static_cast<float>(offset) * scale;
    float ss = static_cast<float>(size) * scale;
    drawZone(vg, drawX + so, drawY + so, ss, ss, "NW", zoneActive[0]);
    drawZone(vg, drawX + drawW - so - ss, drawY + so, ss, ss, "NE", zoneActive[1]);
    drawZone(vg, drawX + so, drawY + drawH - so - ss, ss, ss, "SW", zoneActive[2]);
    drawZone(vg, drawX + drawW - so - ss, drawY + drawH - so - ss, ss, ss, "SE", zoneActive[3]);

    // Instructions
    nvgFontFaceId(vg, 0);
    nvgFontSize(vg, 18.0f);
    nvgFillColor(vg, nvgRGBA(200, 200, 200, 220));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgText(vg, x + width * 0.5f, y + height - 24.0f, "Touch corners to test. Press CIRCLE to exit.", nullptr);

    nvgRestore(vg);
}

FrontTouchPreviewOverlay::FrontTouchPreviewOverlay(int offset, int size)
{
    canvas = new FrontTouchPreviewCanvas(offset, size);
    this->setContentView(canvas);
    this->setTitle("Front Touch Preview");

#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    SceTouchSamplingState currentState = SCE_TOUCH_SAMPLING_STATE_STOP;
    if (sceTouchGetSamplingState(SCE_TOUCH_PORT_FRONT, &currentState) == 0)
    {
        previousFrontSamplingState = currentState;
        restoreSamplingState       = true;
    }
    if (currentState != SCE_TOUCH_SAMPLING_STATE_START)
    {
        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    }
#endif

    this->registerAction(brls::getStr("hints/back"), brls::BUTTON_B, [this](brls::View*)
        {
        brls::Application::popActivity();
        return true; });
}

FrontTouchPreviewOverlay::~FrontTouchPreviewOverlay()
{
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    if (restoreSamplingState)
    {
        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, previousFrontSamplingState);
    }
#endif
}
