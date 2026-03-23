#include "utils/overlay_utils.hpp"

#include <borealis.hpp>
#include <chrono>

BaseOverlay::BaseOverlay()
{
    this->setFocusable(true);
    this->setHideHighlight(true);
    // Do not set background color to maintain transparency

    focusDummy = new FocusDummy();
    focusDummy->setFocusable(true);
    focusDummy->setWidth(1);
    focusDummy->setHeight(1);
    focusDummy->setHideHighlight(true);
    this->addView(focusDummy);

    if (focusDummy)
    {
        focusDummy->registerAction("", brls::BUTTON_NAV_UP, [this](brls::View*)
            {
            this->moveFocus(-1);
            return true; });
        focusDummy->registerAction("", brls::BUTTON_NAV_DOWN, [this](brls::View*)
            {
            this->moveFocus(1);
            return true; });
        focusDummy->registerAction("", brls::BUTTON_A, [this](brls::View*)
            {
            this->activateFocused();
            return true; });
        focusDummy->registerAction(brls::getStr("global/back"), brls::BUTTON_B, [this](brls::View*)
            {
            // By default, activate as if it were resume (index 0)
            if (activateCallback) activateCallback(0);
            return true; });
        focusDummy->registerAction("Cerrar", brls::BUTTON_START, [this](brls::View*)
            {
            // By default, activate as if it were resume
            if (activateCallback) activateCallback(0);
            return true; });
    }

    brls::TapGestureRecognizer* tapRecognizer = new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* sound)
        {
        if (status.state == brls::GestureState::END) {
            float tapX = status.position.x;
            float tapY = status.position.y;
            if (tapX >= this->panelX && tapX <= this->panelX + this->panelW &&
                tapY >= this->panelY && tapY <= this->panelY + this->panelH) {
                float btnStartY = this->panelY + this->btnYStart;
                for (size_t i = 0; i < this->buttonLabels.size(); ++i) {
                    float btnY = btnStartY + i * (this->btnH + this->btnMargin);
                    if (tapY >= btnY && tapY <= btnY + this->btnH) {
                        this->activateFocused(i);
                        break;
                    }
                }
            }
        } });
    this->addGestureRecognizer(tapRecognizer);

    // Don't use brls::sync here - focusDummy may not be valid later
    // Let willAppear() handle initial focus
}

BaseOverlay::~BaseOverlay()
{
    // focusDummy is automatically removed because it is a child
}

void BaseOverlay::setButtons(const std::vector<std::string>& labels)
{
    buttonLabels = labels;
}

void BaseOverlay::moveFocus(int delta)
{
    int numButtons = buttonLabels.size();
    if (numButtons == 0)
        return;
    focusedIndex = (focusedIndex + delta + numButtons) % numButtons;
}

void BaseOverlay::activateFocused(int index)
{
    if (index == -1)
        index = focusedIndex;
    if (activateCallback && index >= 0 && index < (int)buttonLabels.size())
    {
        activateCallback(index);
    }
}

void BaseOverlay::setActivateCallback(std::function<void(int index)> callback)
{
    activateCallback = std::move(callback);
}

void BaseOverlay::setHeaderText(const std::string& text)
{
    headerText = text;
}

void BaseOverlay::setFooterText(const std::string& text)
{
    footerText = text;
}

void BaseOverlay::setPanelPosition(float x, float y)
{
    panelX = x;
    panelY = y;
}

void BaseOverlay::setPanelSize(float w, float h)
{
    panelW = w;
    panelH = h;
}

void BaseOverlay::setPanelAlpha(float alpha)
{
    panelAlpha = alpha;
}

void BaseOverlay::onLayout()
{
    // No children to layout, as we're using pure NVG drawing
}

void BaseOverlay::willAppear(bool resetState)
{
    Box::willAppear(resetState);
    focusedIndex = 0;

    if (focusDummy)
    {
        brls::Application::giveFocus(focusDummy);
    }
}

void BaseOverlay::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    // Dibujar panel background con alpha configurable
    nvgBeginPath(vg);
    nvgRoundedRect(vg, panelX, panelY, panelW, panelH, 8.0f);
    NVGcolor bgColor = nvgRGBA(18, 20, 24, (int)(panelAlpha * 255.0f));
    nvgFillColor(vg, bgColor);
    nvgFill(vg);

    if (!headerText.empty())
    {
        nvgFontSize(vg, 28.0f);
        nvgFontFaceId(vg, 0);
        nvgFillColor(vg, textColor);
        nvgText(vg, panelX + btnXOffset, panelY + 50.0f, headerText.c_str(), nullptr);
    }

    float btnY = panelY + btnYStart;
    nvgFontSize(vg, 22.0f);
    nvgFontFaceId(vg, 0);
    for (size_t i = 0; i < buttonLabels.size(); ++i)
    {
        float btnX = panelX + btnXOffset;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, btnX, btnY, btnW, btnH, 10.0f);
        NVGcolor bgColor = (i == (size_t)focusedIndex) ? btnBgColorFocused : btnBgColorNormal;
        nvgFillColor(vg, bgColor);
        nvgFill(vg);

        nvgStrokeColor(vg, borderColor);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);

        // Texto
        nvgFillColor(vg, textColor);
        nvgText(vg, btnX + 16, btnY + 36, buttonLabels[i].c_str(), nullptr);

        btnY += btnH + btnMargin;
    }

    if (!footerText.empty())
    {
        nvgFontSize(vg, 18.0f);
        nvgFontFaceId(vg, 0);
        nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
        nvgText(vg, panelX + btnXOffset, btnY + 20.0f, footerText.c_str(), nullptr);
    }

    auto t_end      = high_resolution_clock::now();
    auto dur_us     = duration_cast<microseconds>(t_end - t_start).count();
    uint64_t now_ms = duration_cast<milliseconds>(t_end.time_since_epoch()).count();
    if (now_ms - this->lastDrawLogMs > 500)
    {
        this->lastDrawLogMs = now_ms;
        // Optional: log draw time, but silent for now
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

void BaseOverlay::drawFocus(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    // Do not draw the default focus of Borealis
}

void FocusDummy::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    // Don't draw anything
}

void FocusDummy::drawFocus(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    // Don't draw focus
}
