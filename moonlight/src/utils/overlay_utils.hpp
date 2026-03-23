#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class FocusDummy : public brls::View
{
  public:
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

    void drawFocus(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx);
};

class BaseOverlay : public brls::Box
{
  public:
    BaseOverlay();
    ~BaseOverlay() override;

    void setButtons(const std::vector<std::string>& labels);
    void moveFocus(int delta);
    void activateFocused(int index = -1);

    void setActivateCallback(std::function<void(int index)> callback);

    void setHeaderText(const std::string& text);
    void setFooterText(const std::string& text);

    void setPanelPosition(float x, float y);
    void setPanelSize(float w, float h);

    void setPanelAlpha(float alpha);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    void drawFocus(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx);
    void onLayout() override;
    void willAppear(bool resetState = false) override;
    brls::View* getDefaultFocus() override { return focusDummy; }
    bool isTranslucent() override { return true; }

  protected:
    float panelW = 480.0f;
    float panelH = 544.0f;
    float panelX = 800.0f;
    float panelY = 0.0f;

    float btnW       = 424.0f;
    float btnH       = 56.0f;
    float btnXOffset = 28.0f;
    float btnYStart  = 80.0f;
    float btnMargin  = 12.0f;

    NVGcolor panelBgColor      = nvgRGBA(18, 20, 24, 255);
    NVGcolor btnBgColorFocused = nvgRGBA(100, 180, 255, 240);
    NVGcolor btnBgColorNormal  = nvgRGBA(64, 64, 64, 220);
    NVGcolor textColor         = nvgRGBA(255, 255, 255, 255);
    NVGcolor borderColor       = nvgRGBA(255, 255, 255, 120);

  private:
    std::vector<std::string> buttonLabels;
    int focusedIndex       = 0;
    uint64_t lastDrawLogMs = 0;
    FocusDummy* focusDummy = nullptr;
    std::function<void(int index)> activateCallback;
    std::string headerText;
    std::string footerText;
    float panelAlpha = 1.0f;
};
