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
    bool allowsStreamTouch() const override;
    int streamTouchHeight() const override;
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
    bool isSpacerAction(const std::string& action) const;
    bool isModifierAction(const std::string& action) const;
    bool isModifierLatched(const std::string& action) const;
    void finishModifierPress(bool activate);
    void pulseModifier(const std::string& action);
    void toggleModifier(const std::string& action);
    void clearModifiers();
    void applyLatchedModifiers(std::uint64_t releaseAtUs);
    void updateTextPreview(const std::string& action);
    void toggleVideoFocus();
    void updateVideoViewport();
    int videoViewportHeight() const;
    bool mapActionToVk(const std::string& action, int& outVk) const;
    void pulseKey(int vk);
    void releaseExpiredKeys();
    void releaseAllKeys();

    float calcRowWidth(const std::vector<KeyDef>& row, float baseW) const;
    void recalculateKeyMetrics();

    std::string cssPath;
    std::unordered_map<std::string, std::string> properties;
    bool loaded  = false;
    bool visible = false;

    // BaseOverlay keeps panelAlpha private.
    float localPanelAlpha = 1.0f;
    float baseBtnH        = 0.0f;

    int layoutPage = 0;

    int currentLayout   = 0;
    bool forceUtf8Input = false;

    bool showNumbersRow    = true;
    bool showArrowKeys     = true;
    bool fullscreen        = false;
    bool showTextPreview   = true;
    bool mousePassthrough  = false;
    bool showBottomVideo   = false;
    bool manualVideoFocus  = false;
    unsigned int modifiers = 0;
    std::string textPreview;
    float previewHeight = 52.0f;

    int highlightRow    = -1;
    int highlightCol    = -1;
    int highlightFrames = 0;

    int pressedRow     = -1;
    int pressedCol     = -1;
    bool pressedActive = false;
    std::string pressedModifier;
    std::uint64_t modifierPressStartUs = 0;
    bool modifierHoldTriggered         = false;
    std::string repeatAction;
    bool repeatActive           = false;
    std::uint64_t repeatStartUs = 0;
    std::uint64_t repeatLastUs  = 0;
    bool closePending           = false;

    bool keyStates[256];
    std::uint64_t keyReleaseAtUs[256] {};
};
