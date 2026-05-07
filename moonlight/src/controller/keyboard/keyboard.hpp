#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "controller/keyboard/IKeyboard.hpp"
#include "utils/overlay_utils.hpp"

// Forward declare NVGcontext for draw override
struct NVGcontext;

#include <cstring>

// Key definition with variable width support
struct KeyDef
{
    std::string label; // Display label
    std::string action; // Action identifier (same as label by default)
    float widthMult; // Width multiplier (1.0 = normal key width)

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

    // IKeyboard interface
    void open() override;
    void close() override;
    bool isOpen() const override;
    KeyboardState getKeyboardState() const override;
    bool sendsDirectly() const override { return false; }
    bool selfDestructs() const override { return true; }
    void update() override;

    // Load CSS (simple parser) and apply properties
    bool loadCss(const std::string& path);

    // Show/hide keyboard (integration with Borealis is done by whoever pushes the view)
    void show();
    void hide();

    // Parse result exposure for debugging
    std::unordered_map<std::string, std::string> getProperties() const { return properties; }

    void willAppear(bool resetState = false) override;

    // Draw custom keyboard grid
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

  private:
    // Key layout (rows of KeyDef)
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

    // Calculate the total row width for a given row
    float calcRowWidth(const std::vector<KeyDef>& row, float baseW) const;
    void recalculateKeyMetrics();

    std::string cssPath;
    std::unordered_map<std::string, std::string> properties;
    bool loaded  = false;
    bool visible = false;

    // Local copy of panel alpha (BaseOverlay::panelAlpha is private)
    float localPanelAlpha = 1.0f;
    float baseBtnH        = 0.0f;

    // Shift State — toggle when the Shift key is pressed
    bool shiftActive = false;

    // Layout page: 0 = letters, 1 = symbols/numbers
    int layoutPage = 0;

    // Configured layout (0=EN_US, 1=ES_ES, 2=ES_LATAM)
    int currentLayout = 0;

    // Modern layout options
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

    // Visual feedback: index of last tapped key
    int highlightRow    = -1;
    int highlightCol    = -1;
    int highlightFrames = 0;

    // Pressed state and repeat handling
    int pressedRow     = -1;
    int pressedCol     = -1;
    bool pressedActive = false;
    std::string repeatAction;
    bool repeatActive           = false;
    std::uint64_t repeatStartUs = 0;
    std::uint64_t repeatLastUs  = 0;
    bool closePending           = false;

    // Persistent key states
    bool keyStates[256];
};
