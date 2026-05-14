#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "controller/keyboard/IKeyboard.hpp"
#include "utils/overlay_utils.hpp"

struct NVGcontext;

struct KeyDef
{
    std::string label;
    std::string action;
    float widthMult;

    KeyDef(const std::string& lbl, float wMult = 1.0f)
        : label(lbl)
        , action(lbl)
        , widthMult(wMult)
    {
    }
    KeyDef(const std::string& lbl, const std::string& act, float wMult = 1.0f)
        : label(lbl)
        , action(act)
        , widthMult(wMult)
    {
    }
};

class KeyboardOverlay : public BaseOverlay, public IKeyboard
{
  public:
    KeyboardOverlay(const std::string& cssPath);
    ~KeyboardOverlay() override;

    void open() override;
    void close() override;
    bool isOpen() const override;
    KeyboardState getKeyboardState() const override;
    bool sendsDirectly() const override { return false; }
    bool usesNonNormalizedVk() const override { return true; }
    bool selfDestructs() const override { return true; }
    void update() override;

    bool loadCss(const std::string& path);

    void show();
    void hide();

    std::unordered_map<std::string, std::string> getProperties() const { return properties; }

    void willAppear(bool resetState = false) override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

  private:
    std::vector<std::vector<KeyDef>> keyRows;
    void initDefaultLayout();
    void initSymbolLayout();
    void initFnLayout();
    void sendKeyByLabel(const std::string& label);
    void rebuildLayout();
    bool hitTestKey(float x, float y, int& outRow, int& outCol) const;
    bool isRepeatableAction(const std::string& action) const;
    void startRepeat(const std::string& action, int row, int col);
    void stopRepeat();
    void sendRepeatAction(const std::string& action);
    void clearComboSelection();
    void setComboMode(bool enabled);
    bool isSpacerAction(const std::string& action) const;
    std::string formatComboLabel() const;
    bool isModifierAction(const std::string& action) const;
    bool mapActionToVk(const std::string& action, int& outVk) const;

    float calcRowWidth(const std::vector<KeyDef>& row, float baseW) const;
    void recalculateKeyMetrics();

    std::string cssPath;
    std::unordered_map<std::string, std::string> properties;
    bool loaded  = false;
    bool visible = false;

    float localPanelAlpha = 1.0f;
    float baseBtnH        = 0.0f;

    // Shift State — toggle when the Shift key is pressed
    bool shiftActive = false;

    int layoutPage = 0;

    int currentLayout   = 0;
    bool forceUtf8Input = false;

    bool showNumbersRow = true;
    bool showArrowKeys  = true;

    // Combo mode state
    bool comboModeActive   = false;
    unsigned int comboMods = 0;
    std::string comboKeyLabel;
    int comboKeyVk           = -1;
    bool comboHasKey         = false;
    float comboOverlayX      = 0.0f;
    float comboOverlayY      = 0.0f;
    float comboOverlayW      = 0.0f;
    float comboOverlayH      = 0.0f;
    float comboClearX        = 0.0f;
    float comboClearY        = 0.0f;
    float comboClearW        = 0.0f;
    float comboClearH        = 0.0f;
    bool comboOverlayVisible = false;

    int highlightRow    = -1;
    int highlightCol    = -1;
    int highlightFrames = 0;

    int pressedRow     = -1;
    int pressedCol     = -1;
    bool pressedActive = false;
    std::string repeatAction;
    bool repeatActive           = false;
    std::uint64_t repeatStartUs = 0;
    std::uint64_t repeatLastUs  = 0;
    bool closePending           = false;

    bool keyStates[256];
};
