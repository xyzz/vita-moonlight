#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "utils/overlay_utils.hpp"

// Forward declare NVGcontext for draw override
struct NVGcontext;

#include <cstring>

// Keyboard state structure for polling
struct KeyboardState
{
    bool keys[256];

    KeyboardState()
    {
        memset(keys, 0, sizeof(keys));
    }
};

class KeyboardOverlay : public BaseOverlay
{
  public:
    KeyboardOverlay(const std::string& cssPath);
    ~KeyboardOverlay() override;

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

    // Get current keyboard state for polling
    KeyboardState getKeyboardState() const;

  private:
    // Key layout (rows of labels)
    std::vector<std::vector<std::string>> keyRows;
    void initDefaultLayout();
    void sendKeyByLabel(const std::string& label);

    std::string cssPath;
    std::unordered_map<std::string, std::string> properties;
    bool loaded = false;
    // Local copy of panel alpha (BaseOverlay::panelAlpha is private)
    float localPanelAlpha = 1.0f;
    // Shift State (Shift) — toggle when the Shift key is pressed
    bool shiftActive = false;

    // Persistent key states
    bool keyStates[256];
};
