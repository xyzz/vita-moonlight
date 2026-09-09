#include "controller/keyboard/keyboard.hpp"

#include <sys/stat.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#include "controller/keyboard/keyboard_focus.hpp"
#include "controller/keyboard/keyboard_touch.hpp"
#include "controller/keyboard/keyboard_utf8.hpp"
#include "controller/keyboard/keyboardloader.hpp"
#include "debug.hpp"
// Limelight API for keyboard submission
#include "Limelight.h"

// UTF-8 helpers for direct text input
#include <algorithm>
#include <borealis/core/thread.hpp>
#include <borealis/core/time.hpp>
#include <cstdint>

#include "ConfigManager.hpp"
#include "controller/ControllerInput.hpp"
#include "video/VitaVideoRenderer.hpp"

namespace
{

constexpr std::uint64_t kRepeatDelayUs    = 400000;
constexpr std::uint64_t kRepeatIntervalUs = 70000;
constexpr std::uint64_t kModifierHoldUs   = 400000;

enum ModifierMask : unsigned int
{
    MOD_CTRL  = 1u << 0,
    MOD_SHIFT = 1u << 1,
    MOD_ALT   = 1u << 2,
    MOD_WIN   = 1u << 3
};

} // namespace

static bool file_exists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

KeyboardOverlay::KeyboardOverlay(const std::string& cssPath)
    : cssPath(cssPath)
{
    // Auto-load keyboard layouts from XML
    auto_load_keyboard_layouts();

    memset(keyStates, 0, sizeof(keyStates));

    // Neutral default values; `willAppear()` wraps the panel
    // to width/screen and position at appearance time.
    panelW   = 0.0f;
    panelH   = 260.0f;
    panelX   = 0.0f;
    panelY   = 0.0f;
    baseBtnH = btnH;
    loadCss(cssPath);
    {
        ConfigManager config;
        config.load();
        VideoSettings settings = config.getVideoSettings();
        currentLayout          = settings.keyboard_layout;
        forceUtf8Input         = settings.keyboard_input_mode;
        showNumbersRow         = settings.keyboard_numbers_row;
        showArrowKeys          = settings.keyboard_show_arrows;
        fullscreen             = settings.keyboard_fullscreen;
        showTextPreview        = settings.keyboard_text_preview;
        mousePassthrough       = settings.keyboard_mouse_passthrough;
    }
    initDefaultLayout();

    // Register recognizer for touch press/hold
    auto* touchRecognizer = new KeyboardTouchRecognizer([this](brls::GestureState state, const brls::Point& position)
        {
        const float tapX = position.x;
        const float tapY = position.y;

        const bool inPanel =
            (tapX >= panelX && tapX <= panelX + panelW &&
             tapY >= panelY && tapY <= panelY + panelH);

        if (state == brls::GestureState::START) {
            if (!inPanel) return false;
            int row = -1;
            int col = -1;
            if (hitTestKey(tapX, tapY, row, col)) {
                pressedRow = row;
                pressedCol = col;
                pressedActive = true;

                highlightRow = row;
                highlightCol = col;
                highlightFrames = 6;

                const std::string action = keyRows[row][col].action;
                if (isModifierAction(action))
                {
                    pressedModifier      = action;
                    modifierPressStartUs = static_cast<std::uint64_t>(brls::getCPUTimeUsec());
                    modifierHoldTriggered = false;
                    return true;
                }

                const bool modifiedShortcut = (modifiers & (MOD_CTRL | MOD_ALT | MOD_WIN)) != 0;
                sendKeyByLabel(action);
                if (!modifiedShortcut)
                {
                    startRepeat(action, row, col);
                }
            }
        } else if (state == brls::GestureState::STAY) {
            if (!pressedActive) return true;
            int row = -1;
            int col = -1;
            if (!hitTestKey(tapX, tapY, row, col) || row != pressedRow || col != pressedCol) {
                stopRepeat();
                finishModifierPress(false);
                pressedActive = false;
                pressedRow = -1;
                pressedCol = -1;
            }
        } else if (state == brls::GestureState::END || state == brls::GestureState::FAILED || state == brls::GestureState::INTERRUPTED) {
            int row = -1;
            int col = -1;
            const bool sameKey = inPanel && hitTestKey(tapX, tapY, row, col) && row == pressedRow && col == pressedCol;
            stopRepeat();
            finishModifierPress(state == brls::GestureState::END && sameKey);
            pressedActive = false;
            pressedRow = -1;
            pressedCol = -1;
        }
        return true; });
    this->addGestureRecognizer(touchRecognizer);

    // Register with input manager
    if (g_controllerInput)
    {
        g_controllerInput->setActiveKeyboard(this);
    }

    visible = true;
}

KeyboardOverlay::~KeyboardOverlay()
{
    cancel_keyboard_typing_focus();
    releaseAllKeys();
    VitaVideoRenderer::instance().clearKeyboardViewport();
    // Unregister
    if (g_controllerInput && g_controllerInput->getActiveKeyboard() == this)
    {
        g_controllerInput->setActiveKeyboard(nullptr);
    }
    visible = false;
}

// IKeyboard interface
void KeyboardOverlay::open()
{
    show();
    visible      = true;
    closePending = false;
    updateVideoViewport();
}

void KeyboardOverlay::close()
{
    cancel_keyboard_typing_focus();
    closePending = true;
}

bool KeyboardOverlay::isOpen() const
{
    return visible;
}

bool KeyboardOverlay::allowsStreamTouch() const
{
    return visible && !fullscreen && mousePassthrough;
}

int KeyboardOverlay::streamTouchHeight() const
{
    return allowsStreamTouch() ? videoViewportHeight() : 0;
}

KeyboardState KeyboardOverlay::getKeyboardState() const
{
    KeyboardState state;
    memcpy(state.keys, keyStates, sizeof(keyStates));
    return state;
}

void KeyboardOverlay::update()
{
    releaseExpiredKeys();

    if (pressedActive && !pressedModifier.empty() && !modifierHoldTriggered)
    {
        const std::uint64_t nowUs = static_cast<std::uint64_t>(brls::getCPUTimeUsec());
        if (nowUs >= modifierPressStartUs && nowUs - modifierPressStartUs >= kModifierHoldUs)
        {
            toggleModifier(pressedModifier);
            modifierHoldTriggered = true;
        }
    }

    if (closePending)
    {
        closePending = false;
        brls::sync([this]()
            {
            stopRepeat();
            finishModifierPress(false);
            pressedActive = false;
            pressedRow = -1;
            pressedCol = -1;
            releaseAllKeys();
            textPreview.clear();
            hide();
            visible = false;
            VitaVideoRenderer::instance().clearKeyboardViewport();
            if (g_controllerInput && g_controllerInput->getActiveKeyboard() == this) {
                g_controllerInput->setActiveKeyboard(nullptr);
            }
            brls::Application::popActivity(); });
    }

    if (!repeatActive)
    {
        return;
    }

    const std::uint64_t nowUs = static_cast<std::uint64_t>(brls::getCPUTimeUsec());
    if (nowUs < repeatStartUs || (nowUs - repeatStartUs) < kRepeatDelayUs)
    {
        return;
    }

    if (repeatLastUs == 0 || (nowUs - repeatLastUs) >= kRepeatIntervalUs)
    {
        repeatLastUs = nowUs;
        sendRepeatAction(repeatAction);
    }
}

bool KeyboardOverlay::loadCss(const std::string& path)
{
    properties.clear();
    if (!file_exists(path))
    {
        std::cerr << "[KeyboardOverlay] CSS file not found: " << path << std::endl;
        loaded = false;
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "[KeyboardOverlay] Unable to open CSS: " << path << std::endl;
        loaded = false;
        return false;
    }
    std::string line;
    while (std::getline(file, line))
    {
        // Trim
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;
        size_t end    = line.find_last_not_of(" \t\r\n");
        std::string t = line.substr(start, end - start + 1);
        if (t.empty() || t[0] == '#')
            continue;
        size_t sep = t.find(":");
        if (sep == std::string::npos)
            continue;
        std::string key = t.substr(0, sep);
        std::string val = t.substr(sep + 1);
        // trim both
        auto trim = [](std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos)
            {
                s.clear();
                return;
            }
            size_t b = s.find_last_not_of(" \t\r\n");
            s        = s.substr(a, b - a + 1);
        };
        trim(key);
        trim(val);
        if (!key.empty())
            properties[key] = val;
    }
    file.close();
    loaded = true;
    return true;
}

void KeyboardOverlay::show()
{
    this->setVisibility(brls::Visibility::VISIBLE);
}

void KeyboardOverlay::hide()
{
    this->setVisibility(brls::Visibility::GONE);
}

void KeyboardOverlay::willAppear(bool resetState)
{
    BaseOverlay::willAppear(resetState);
    if (!loaded)
    {
        // Still work without CSS - just use defaults
    }
    auto it = properties.find("transparent");
    if (it != properties.end())
    {
        std::string v = it->second;
        if (v == "true" || v == "1")
        {
            this->setPanelAlpha(0.0f);
            this->localPanelAlpha = 0.0f;
        }
        else
        {
            this->setPanelAlpha(1.0f);
            this->localPanelAlpha = 1.0f;
        }
    }
    it = properties.find("key-width");
    if (it != properties.end())
    {
        try
        {
            btnW = std::stof(it->second);
        }
        catch (...)
        {
        }
    }
    it = properties.find("key-height");
    if (it != properties.end())
    {
        try
        {
            btnH = std::stof(it->second);
        }
        catch (...)
        {
        }
        baseBtnH = btnH;
    }
    it = properties.find("key-margin");
    if (it != properties.end())
    {
        try
        {
            btnMargin = std::stof(it->second);
        }
        catch (...)
        {
        }
    }
    it = properties.find("background-image");
    if (it != properties.end() && !it->second.empty())
    {
        // Not implemented: load background image for keyboard overlay.
        vita_log::info("[KeyboardOverlay] background-image specified: %s", it->second.c_str());
    }

    // Adjust panel so that it fills the entire width and is at the bottom
    try
    {
        const float contentWidth  = static_cast<float>(this->getWidth());
        const float contentHeight = static_cast<float>(this->getHeight());
        // If the CSS specifies panel-height, use it
        auto pit = properties.find("panel-height");
        if (pit != properties.end())
        {
            try
            {
                panelH = std::stof(pit->second);
            }
            catch (...)
            {
            }
        }
        if (fullscreen)
        {
            panelH = contentHeight;
        }
        else
        {
            const float maxPanelH = contentHeight * 0.5f;
            if (panelH > maxPanelH)
            {
                panelH = maxPanelH;
            }
        }
        panelW = contentWidth;
        panelX = 0.0f;
        panelY = fullscreen ? 0.0f : std::max(0.0f, contentHeight - panelH);

        recalculateKeyMetrics();
        updateVideoViewport();
        if (!fullscreen && !manualVideoFocus)
        {
            const int viewportHeight = videoViewportHeight();
            vita_log::info("[KeyboardOverlay] Video viewport=%d keyboard=%d", viewportHeight,
                static_cast<int>(brls::Application::windowHeight) - viewportHeight);
            request_keyboard_typing_focus([this](float normalizedY)
                {
                const int viewportHeight = videoViewportHeight();
                if (!visible || fullscreen || viewportHeight <= 0) {
                    return;
                }
                showBottomVideo = normalizedY * static_cast<float>(brls::Application::windowHeight) >
                    static_cast<float>(viewportHeight);
                updateVideoViewport(); });
        }
    }
    catch (...)
    {
    }
}

void KeyboardOverlay::initDefaultLayout()
{
    keyRows.clear();
    layoutPage                   = 0;
    const bool isSpanish         = (currentLayout == 1 || currentLayout == 2);
    const std::string spaceLabel = isSpanish
        ? (currentLayout == 2 ? u8"Español (LATAM)" : u8"Español (ES)")
        : "English (US)";

    if (showNumbersRow)
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer"),
            KeyDef("1"), KeyDef("2"), KeyDef("3"), KeyDef("4"), KeyDef("5"),
            KeyDef("6"), KeyDef("7"), KeyDef("8"), KeyDef("9"), KeyDef("0"),
            KeyDef("Spacer", "Spacer") });
    }

    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("q"), KeyDef("w"), KeyDef("e"), KeyDef("r"), KeyDef("t"),
        KeyDef("y"), KeyDef("u"), KeyDef("i"), KeyDef("o"), KeyDef("p"),
        KeyDef("Spacer", "Spacer") });

    if (isSpanish)
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer"),
            KeyDef("a"), KeyDef("s"), KeyDef("d"), KeyDef("f"), KeyDef("g"),
            KeyDef("h"), KeyDef("j"), KeyDef("k"), KeyDef("l"), KeyDef(u8"\u00F1"),
            KeyDef("Spacer", "Spacer") });
    }
    else
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer"),
            KeyDef("a"), KeyDef("s"), KeyDef("d"), KeyDef("f"), KeyDef("g"),
            KeyDef("h"), KeyDef("j"), KeyDef("k"), KeyDef("l"), KeyDef(";"),
            KeyDef("Spacer", "Spacer") });
    }

    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("Shift", 1.5f),
        KeyDef("z"), KeyDef("x"), KeyDef("c"), KeyDef("v"),
        KeyDef("b"), KeyDef("n"), KeyDef("m"),
        KeyDef("Bksp", "Backspace", 1.5f),
        KeyDef("Spacer", "Spacer") });

    keyRows.push_back({ fullscreen ? KeyDef("Spacer", "Spacer", 1.2f) : KeyDef("View", "VideoFocus", 1.2f),
        KeyDef("Fn", "FnPage", 1.0f),
        KeyDef("1#1", "SymPage", 1.2f),
        KeyDef("Spacer", "Spacer", 4.0f),
        KeyDef(","),
        KeyDef(spaceLabel, "Space", 3.2f),
        KeyDef("."),
        KeyDef("Enter", "Enter", 1.4f),
        KeyDef("X", "Close", 1.0f) });

    if (showArrowKeys)
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer", 5.0f),
            KeyDef("<", "Left", 1.1f), KeyDef("v", "Down", 1.1f), KeyDef(">", "Right", 1.1f), KeyDef("^", "Up", 1.2f) });
    }

    keyRows.insert(keyRows.begin(), { KeyDef("Ctrl", "Ctrl", 1.2f), KeyDef("Alt", "Alt", 1.2f), KeyDef("Win", "Win", 1.2f), KeyDef("Esc", "Escape", 1.2f) });

    recalculateKeyMetrics();
}

// Symbol/number layout page
void KeyboardOverlay::initSymbolLayout()
{
    keyRows.clear();
    layoutPage                   = 1;
    const bool isSpanish         = (currentLayout == 1 || currentLayout == 2);
    const std::string spaceLabel = isSpanish
        ? (currentLayout == 2 ? u8"Español (LATAM)" : u8"Español (ES)")
        : "English (US)";

    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("1"), KeyDef("2"), KeyDef("3"), KeyDef("4"), KeyDef("5"),
        KeyDef("6"), KeyDef("7"), KeyDef("8"), KeyDef("9"), KeyDef("0"),
        KeyDef("Spacer", "Spacer") });
    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("@"), KeyDef("#"), KeyDef("$"), KeyDef("%"), KeyDef("&"),
        KeyDef("*"), KeyDef("-"), KeyDef("+"), KeyDef("_"), KeyDef("="),
        KeyDef("("), KeyDef(")"),
        KeyDef("Spacer", "Spacer") });

    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("["), KeyDef("]"), KeyDef("{"), KeyDef("}"), KeyDef("<"), KeyDef(">"),
        KeyDef("/"), KeyDef("\\"), KeyDef("|"), KeyDef("^"), KeyDef("~"), KeyDef("`"),
        KeyDef("Spacer", "Spacer") });

    if (isSpanish)
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer"),
            KeyDef(u8"¿"), KeyDef(u8"¡"), KeyDef(u8"º"), KeyDef(u8"ª"),
            KeyDef(u8"€"), KeyDef(u8"£"), KeyDef(u8"¥"), KeyDef(u8"¢"), KeyDef(u8"°"),
            KeyDef("Spacer", "Spacer") });
    }
    else
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer"),
            KeyDef(u8"€"), KeyDef(u8"£"), KeyDef(u8"¥"), KeyDef(u8"¢"), KeyDef(u8"°"),
            KeyDef(u8"±"), KeyDef(u8"×"), KeyDef(u8"÷"), KeyDef(u8"§"),
            KeyDef("Spacer", "Spacer") });
    }

    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("Shift", 1.5f),
        KeyDef("!"), KeyDef("?"), KeyDef("\""), KeyDef("'"),
        KeyDef(":"), KeyDef(";"), KeyDef(","), KeyDef("."),
        KeyDef("Bksp", "Backspace", 1.5f),
        KeyDef("Spacer", "Spacer") });

    keyRows.push_back({ KeyDef("ABC", "LetterPage", 1.2f),
        fullscreen ? KeyDef("Spacer", "Spacer", 1.2f) : KeyDef("View", "VideoFocus", 1.2f),
        KeyDef("Fn", "FnPage", 1.0f),
        KeyDef("Spacer", "Spacer", 4.0f),
        KeyDef(","),
        KeyDef(spaceLabel, "Space", 3.2f),
        KeyDef("."),
        KeyDef("Enter", "Enter", 1.4f),
        KeyDef("X", "Close", 1.0f) });

    if (showArrowKeys)
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer", 5.0f),
            KeyDef("<", "Left", 1.1f), KeyDef("v", "Down", 1.1f), KeyDef(">", "Right", 1.1f), KeyDef("^", "Up", 1.2f) });
    }

    keyRows.insert(keyRows.begin(), { KeyDef("Ctrl", "Ctrl", 1.2f), KeyDef("Alt", "Alt", 1.2f), KeyDef("Win", "Win", 1.2f) });

    recalculateKeyMetrics();
}

// Function key layout page
void KeyboardOverlay::initFnLayout()
{
    keyRows.clear();
    layoutPage                   = 2;
    const bool isSpanish         = (currentLayout == 1 || currentLayout == 2);
    const std::string spaceLabel = isSpanish
        ? (currentLayout == 2 ? u8"Español (LATAM)" : u8"Español (ES)")
        : "English (US)";

    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("F1"), KeyDef("F2"), KeyDef("F3"), KeyDef("F4"), KeyDef("F5"), KeyDef("F6"),
        KeyDef("F7"), KeyDef("F8"), KeyDef("F9"), KeyDef("F10"), KeyDef("F11"), KeyDef("F12"),
        KeyDef("Spacer", "Spacer") });

    if (showArrowKeys)
    {
        keyRows.push_back({ KeyDef("PrtSc", "PrintScreen"),
            KeyDef("ScrLk", "ScrollLock"),
            KeyDef("Pause", "Pause"),
            KeyDef("Ins", "Insert"),
            KeyDef("Del", "Delete"),
            KeyDef("Spacer", "Spacer"),
            KeyDef("<", "Left", 1.1f), KeyDef("v", "Down", 1.1f), KeyDef(">", "Right", 1.1f), KeyDef("^", "Up", 1.2f) });
    }
    else
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer"),
            KeyDef("PrtSc", "PrintScreen"),
            KeyDef("ScrLk", "ScrollLock"),
            KeyDef("Pause", "Pause"),
            KeyDef("Ins", "Insert"),
            KeyDef("Del", "Delete"),
            KeyDef("Spacer", "Spacer") });
    }

    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("Home", "Home"),
        KeyDef("End", "End"),
        KeyDef("PgUp", "PageUp"),
        KeyDef("PgDn", "PageDown"),
        KeyDef("Esc", "Escape"),
        KeyDef("Tab"),
        KeyDef("Spacer", "Spacer") });

    keyRows.push_back({ KeyDef("Spacer", "Spacer"),
        KeyDef("Ctrl", "Ctrl", 1.2f),
        KeyDef("Alt", "Alt", 1.2f),
        KeyDef("Win", "Win", 1.2f),
        KeyDef("Spacer", "Spacer") });

    keyRows.push_back({ KeyDef("ABC", "LetterPage", 1.2f),
        KeyDef("1#1", "SymPage", 1.2f),
        fullscreen ? KeyDef("Spacer", "Spacer", 1.2f) : KeyDef("View", "VideoFocus", 1.2f),
        KeyDef("Spacer", "Spacer", 4.0f),
        KeyDef(spaceLabel, "Space", 3.4f),
        KeyDef("Enter", "Enter", 1.4f),
        KeyDef("X", "Close", 1.0f) });

    recalculateKeyMetrics();
}

void KeyboardOverlay::rebuildLayout()
{
    switch (layoutPage)
    {
        case 1:
            initSymbolLayout();
            break;
        case 2:
            initFnLayout();
            break;
        case 0:
        default:
            initDefaultLayout();
            break;
    }
}

void KeyboardOverlay::recalculateKeyMetrics()
{
    if (panelW <= 0.0f || panelH <= 0.0f)
    {
        return;
    }

    float usable       = panelW - 2.0f * btnXOffset;
    float minCandidate = 0.0f;
    bool hasCandidate  = false;

    for (auto& r : keyRows)
    {
        float units = 0.0f;
        for (auto& k : r)
        {
            if (k.action == "Spacer")
                continue;
            units += k.widthMult;
        }
        if (units <= 0.0f)
            continue;

        float margins          = r.size() > 0 ? (float)(r.size() - 1) * btnMargin : 0.0f;
        float availableForKeys = usable - margins;
        if (availableForKeys <= 0.0f)
            continue;

        float candidate = availableForKeys / units;
        if (!hasCandidate || candidate < minCandidate)
        {
            minCandidate = candidate;
            hasCandidate = true;
        }
    }

    if (hasCandidate && minCandidate > 8.0f)
    {
        btnW = minCandidate;
    }

    btnYStart = fullscreen && showTextPreview ? previewHeight + 8.0f : 8.0f;

    if (!keyRows.empty())
    {
        float availableH   = panelH - btnYStart - 6.0f;
        float totalMargins = (float)(keyRows.size() - 1) * btnMargin;
        float maxKeyH      = (availableH - totalMargins) / (float)keyRows.size();
        float targetH      = maxKeyH;
        if (!fullscreen && baseBtnH > 0.0f && targetH > baseBtnH)
        {
            targetH = baseBtnH;
        }
        if (targetH > 8.0f)
        {
            btnH = targetH;
        }
    }
}

float KeyboardOverlay::calcRowWidth(const std::vector<KeyDef>& row, float baseW) const
{
    if (row.empty())
        return 0;
    float total = 0;
    for (size_t i = 0; i < row.size(); ++i)
    {
        total += baseW * row[i].widthMult;
        if (i < row.size() - 1)
            total += btnMargin;
    }
    return total;
}

bool KeyboardOverlay::hitTestKey(float x, float y, int& outRow, int& outCol) const
{
    float startY   = panelY + btnYStart;
    float baseKeyW = btnW;
    for (size_t row = 0; row < keyRows.size(); ++row)
    {
        float rowY = startY + row * (btnH + btnMargin);
        if (y < rowY || y > rowY + btnH)
        {
            continue;
        }

        const auto& cols = keyRows[row];
        if (cols.empty())
        {
            continue;
        }
        int spacerCount = 0;
        float fixedW    = 0.0f;
        for (const auto& key : cols)
        {
            if (key.action == "Spacer")
            {
                spacerCount++;
            }
            else
            {
                fixedW += baseKeyW * key.widthMult;
            }
        }
        float available = panelW - 2.0f * btnXOffset;
        float margins   = cols.size() > 0 ? (float)(cols.size() - 1) * btnMargin : 0.0f;
        float remaining = available - fixedW - margins;
        float spacerW   = (spacerCount > 0 && remaining > 0.0f) ? (remaining / (float)spacerCount) : 0.0f;
        float startX    = panelX + btnXOffset;
        float keyX      = startX;
        for (size_t col = 0; col < cols.size(); ++col)
        {
            float kw = cols[col].action == "Spacer" ? spacerW : baseKeyW * cols[col].widthMult;
            if (cols[col].action == "Spacer")
            {
                keyX += kw + btnMargin;
                continue;
            }
            if (x >= keyX && x <= keyX + kw)
            {
                outRow = static_cast<int>(row);
                outCol = static_cast<int>(col);
                return true;
            }
            keyX += kw + btnMargin;
        }
    }
    return false;
}

bool KeyboardOverlay::isRepeatableAction(const std::string& action) const
{
    if (action == "Close" || action == "SymPage" || action == "LetterPage" || action == "FnPage" || action == "VideoFocus" || action == "Shift" || action == "Ctrl" || action == "Alt" || action == "Win")
    {
        return false;
    }

    if (action == "Backspace" || action == "Space" || action == "Enter" || action == "Tab" || action == "Escape" || action == "Up" || action == "Down" || action == "Left" || action == "Right")
    {
        return true;
    }

    std::uint32_t codepoint = 0;
    if (decode_single_utf8(action, codepoint))
    {
        return true;
    }

    return false;
}

void KeyboardOverlay::startRepeat(const std::string& action, int row, int col)
{
    (void)row;
    (void)col;
    if (!isRepeatableAction(action))
    {
        return;
    }
    repeatActive  = true;
    repeatAction  = action;
    repeatStartUs = static_cast<std::uint64_t>(brls::getCPUTimeUsec());
    repeatLastUs  = 0;
}

void KeyboardOverlay::stopRepeat()
{
    repeatActive = false;
    repeatAction.clear();
    repeatStartUs = 0;
    repeatLastUs  = 0;
}

void KeyboardOverlay::sendRepeatAction(const std::string& action)
{
    sendKeyByLabel(action);
}

bool KeyboardOverlay::isSpacerAction(const std::string& action) const
{
    return action == "Spacer";
}

bool KeyboardOverlay::isModifierAction(const std::string& action) const
{
    return action == "Ctrl" || action == "Shift" || action == "Alt" || action == "Win";
}

bool KeyboardOverlay::isModifierLatched(const std::string& action) const
{
    if (action == "Ctrl")
        return (modifiers & MOD_CTRL) != 0;
    if (action == "Shift")
        return (modifiers & MOD_SHIFT) != 0;
    if (action == "Alt")
        return (modifiers & MOD_ALT) != 0;
    if (action == "Win")
        return (modifiers & MOD_WIN) != 0;
    return false;
}

void KeyboardOverlay::finishModifierPress(bool activate)
{
    if (activate && !pressedModifier.empty() && !modifierHoldTriggered)
    {
        pulseModifier(pressedModifier);
    }
    pressedModifier.clear();
    modifierPressStartUs  = 0;
    modifierHoldTriggered = false;
}

void KeyboardOverlay::pulseModifier(const std::string& action)
{
    int vk = -1;
    if (action == "Ctrl")
        vk = 0x11;
    else if (action == "Shift")
        vk = 0x10;
    else if (action == "Alt")
        vk = 0x12;
    else if (action == "Win")
        vk = 0x5B;

    if (vk >= 0)
    {
        pulseKey(vk);
    }
}

void KeyboardOverlay::toggleModifier(const std::string& action)
{
    unsigned int mask = 0;
    if (action == "Ctrl")
        mask = MOD_CTRL;
    else if (action == "Shift")
        mask = MOD_SHIFT;
    else if (action == "Alt")
        mask = MOD_ALT;
    else if (action == "Win")
        mask = MOD_WIN;

    if (mask != 0)
    {
        modifiers ^= mask;
        stopRepeat();
    }
}

void KeyboardOverlay::clearModifiers()
{
    modifiers = 0;
}

void KeyboardOverlay::applyLatchedModifiers(std::uint64_t releaseAtUs)
{
    const struct
    {
        unsigned int mask;
        int vk;
    } modifierKeys[] = {
        { MOD_CTRL, 0x11 },
        { MOD_SHIFT, 0x10 },
        { MOD_ALT, 0x12 },
        { MOD_WIN, 0x5B },
    };

    for (const auto& modifier : modifierKeys)
    {
        if ((modifiers & modifier.mask) != 0)
        {
            keyStates[modifier.vk]      = true;
            keyReleaseAtUs[modifier.vk] = releaseAtUs;
        }
    }
}

void KeyboardOverlay::updateTextPreview(const std::string& action)
{
    if (!showTextPreview)
    {
        return;
    }

    if (action == "Backspace")
    {
        if (!textPreview.empty())
        {
            std::size_t pos = textPreview.size() - 1;
            while (pos > 0 && (static_cast<unsigned char>(textPreview[pos]) & 0xC0) == 0x80)
            {
                --pos;
            }
            textPreview.erase(pos);
        }
        return;
    }
    if (action == "Enter")
    {
        textPreview.clear();
        return;
    }
    if (action == "Space")
    {
        textPreview.push_back(' ');
    }
    else
    {
        std::uint32_t codepoint = 0;
        if (!decode_single_utf8(action, codepoint))
        {
            return;
        }
        if ((modifiers & MOD_SHIFT) != 0)
        {
            codepoint = apply_shift_to_codepoint(codepoint);
        }
        textPreview += utf8_from_codepoint(codepoint);
    }

    while (textPreview.size() > 96)
    {
        std::size_t count = 1;
        while (count < textPreview.size() && (static_cast<unsigned char>(textPreview[count]) & 0xC0) == 0x80)
        {
            ++count;
        }
        textPreview.erase(0, count);
    }
}

void KeyboardOverlay::toggleVideoFocus()
{
    if (fullscreen)
    {
        return;
    }
    manualVideoFocus = true;
    showBottomVideo  = !showBottomVideo;
    cancel_keyboard_typing_focus();
    updateVideoViewport();
    vita_log::info("[KeyboardOverlay] Manual video view=%s viewport=%d",
        showBottomVideo ? "bottom" : "top", videoViewportHeight());
}

void KeyboardOverlay::updateVideoViewport()
{
    const int viewportHeight = videoViewportHeight();
    if (visible && !fullscreen && viewportHeight > 0)
    {
        VitaVideoRenderer::instance().setKeyboardViewport(viewportHeight, showBottomVideo);
    }
    else
    {
        VitaVideoRenderer::instance().clearKeyboardViewport();
    }
}

int KeyboardOverlay::videoViewportHeight() const
{
    const int height = static_cast<int>(std::lround(panelY * brls::Application::windowScale));
    return std::max(0, std::min(static_cast<int>(brls::Application::windowHeight), height));
}

bool KeyboardOverlay::mapActionToVk(const std::string& action, int& outVk) const
{
    if (action == "Enter")
    {
        outVk = 0x0D;
        return true;
    }
    if (action == "Tab")
    {
        outVk = 0x09;
        return true;
    }
    if (action == "Escape")
    {
        outVk = 0x1B;
        return true;
    }
    if (action == "Backspace")
    {
        outVk = 0x08;
        return true;
    }
    if (action == "Space")
    {
        outVk = 0x20;
        return true;
    }
    if (action == "Left")
    {
        outVk = 0x25;
        return true;
    }
    if (action == "Up")
    {
        outVk = 0x26;
        return true;
    }
    if (action == "Right")
    {
        outVk = 0x27;
        return true;
    }
    if (action == "Down")
    {
        outVk = 0x28;
        return true;
    }
    if (action == "Insert")
    {
        outVk = 0x2D;
        return true;
    }
    if (action == "Delete")
    {
        outVk = 0x2E;
        return true;
    }
    if (action == "Home")
    {
        outVk = 0x24;
        return true;
    }
    if (action == "End")
    {
        outVk = 0x23;
        return true;
    }
    if (action == "PageUp")
    {
        outVk = 0x21;
        return true;
    }
    if (action == "PageDown")
    {
        outVk = 0x22;
        return true;
    }
    if (action == "PrintScreen")
    {
        outVk = 0x2C;
        return true;
    }
    if (action == "ScrollLock")
    {
        outVk = 0x91;
        return true;
    }
    if (action == "Pause")
    {
        outVk = 0x13;
        return true;
    }

    if (!action.empty() && action[0] == 'F')
    {
        int num = 0;
        try
        {
            num = std::stoi(action.substr(1));
        }
        catch (...)
        {
            num = 0;
        }
        if (num >= 1 && num <= 12)
        {
            outVk = 0x70 + (num - 1);
            return true;
        }
    }

    std::uint32_t codepoint = 0;
    if (decode_single_utf8(action, codepoint) && codepoint <= 0x7F)
    {
        char c = static_cast<char>(codepoint);
        if (c >= 'a' && c <= 'z')
        {
            outVk = c - 'a' + 'A';
            return true;
        }
        if (c >= 'A' && c <= 'Z')
        {
            outVk = c;
            return true;
        }
        if (c >= '0' && c <= '9')
        {
            outVk = c;
            return true;
        }
        switch (c)
        {
            case '@':
                outVk = 0x32;
                return true;
            case '#':
                outVk = 0x33;
                return true;
            case '$':
                outVk = 0x34;
                return true;
            case '%':
                outVk = 0x35;
                return true;
            case '^':
                outVk = 0x36;
                return true;
            case '&':
                outVk = 0x37;
                return true;
            case '*':
                outVk = 0x38;
                return true;
            case '(':
                outVk = 0x39;
                return true;
            case ')':
                outVk = 0x30;
                return true;
            case ',':
                outVk = 0xBC;
                return true;
            case '<':
                outVk = 0xBC;
                return true;
            case '.':
                outVk = 0xBE;
                return true;
            case '>':
                outVk = 0xBE;
                return true;
            case '/':
                outVk = 0xBF;
                return true;
            case ';':
                outVk = 0xBA;
                return true;
            case ':':
                outVk = 0xBA;
                return true;
            case '\\':
                outVk = 0xDC;
                return true;
            case '|':
                outVk = 0xDC;
                return true;
            case '[':
                outVk = 0xDB;
                return true;
            case ']':
                outVk = 0xDD;
                return true;
            case '{':
                outVk = 0xDB;
                return true;
            case '}':
                outVk = 0xDD;
                return true;
            case '-':
                outVk = 0xBD;
                return true;
            case '_':
                outVk = 0xBD;
                return true;
            case '=':
                outVk = 0xBB;
                return true;
            case '+':
                outVk = 0xBB;
                return true;
            case '`':
                outVk = 0xC0;
                return true;
            case '~':
                outVk = 0xC0;
                return true;
            case '\'':
                outVk = 0xDE;
                return true;
            case '"':
                outVk = 0xDE;
                return true;
            case '!':
                outVk = 0x31;
                return true;
            case '?':
                outVk = 0xBF;
                return true;
            default:
                break;
        }
    }

    return false;
}

void KeyboardOverlay::pulseKey(int vk)
{
    if (vk < 0 || vk >= 256)
    {
        return;
    }
    const std::uint64_t releaseAt = static_cast<std::uint64_t>(brls::getCPUTimeUsec()) + 50000;
    const bool isModifierKey      = vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0x5B;
    if (!isModifierKey)
    {
        applyLatchedModifiers(releaseAt);
    }
    keyStates[vk]      = true;
    keyReleaseAtUs[vk] = releaseAt;

    for (int modifier : { 0x11, 0x12, 0x5B })
    {
        if (keyStates[modifier])
        {
            keyReleaseAtUs[modifier] = releaseAt;
        }
    }
    if (!isModifierKey)
    {
        clearModifiers();
    }
}

void KeyboardOverlay::releaseExpiredKeys()
{
    const std::uint64_t nowUs = static_cast<std::uint64_t>(brls::getCPUTimeUsec());
    for (int vk = 0; vk < 256; ++vk)
    {
        if (keyReleaseAtUs[vk] != 0 && nowUs >= keyReleaseAtUs[vk])
        {
            keyStates[vk]      = false;
            keyReleaseAtUs[vk] = 0;
        }
    }
}

void KeyboardOverlay::releaseAllKeys()
{
    for (int vk = 0; vk < 256; ++vk)
    {
        if (keyStates[vk])
        {
            LiSendKeyboardEvent2(static_cast<short>(vk), KEY_ACTION_UP, 0, SS_KBE_FLAG_NON_NORMALIZED);
        }
        keyStates[vk]      = false;
        keyReleaseAtUs[vk] = 0;
    }
    finishModifierPress(false);
    clearModifiers();
}

void KeyboardOverlay::sendKeyByLabel(const std::string& label)
{
    if (label == "Close")
    {
        closePending = true;
        return;
    }

    if (label == "Spacer")
    {
        return;
    }

    if (label == "SymPage")
    {
        initSymbolLayout();
        return;
    }

    if (label == "LetterPage")
    {
        initDefaultLayout();
        return;
    }

    if (label == "FnPage")
    {
        initFnLayout();
        return;
    }

    if (label == "VideoFocus")
    {
        toggleVideoFocus();
        return;
    }

    if (isModifierAction(label))
    {
        toggleModifier(label);
        return;
    }

    if (label == "Tab")
    {
        pulseKey(0x09);
        return;
    }

    if (label == "Escape")
    {
        pulseKey(0x1B);
        return;
    }

    if (label == "Space")
    {
        updateTextPreview(label);
        pulseKey(0x20);
        return;
    }
    if (label == "Enter")
    {
        updateTextPreview(label);
        pulseKey(0x0D);
        return;
    }
    if (label == "Backspace")
    {
        updateTextPreview(label);
        pulseKey(0x08);
        return;
    }

    std::uint32_t codepoint = 0;
    if (!decode_single_utf8(label, codepoint))
    {
        int vk = -1;
        if (mapActionToVk(label, vk))
        {
            pulseKey(vk);
        }
        return;
    }

    if ((modifiers & MOD_SHIFT) != 0)
    {
        codepoint = apply_shift_to_codepoint(codepoint);
    }

    const bool needsKeyEvent = (modifiers & (MOD_CTRL | MOD_ALT | MOD_WIN)) != 0;
    if (!forceUtf8Input || needsKeyEvent)
    {
        VkMapping mapping;
        if (lookup_vk_mapping(static_cast<KeyboardLayout>(currentLayout), codepoint, mapping))
        {
            short vk                = mapping.vk;
            bool shiftAlreadyActive = keyStates[0x10] || (modifiers & MOD_SHIFT) != 0;
            bool needShift          = mapping.needs_shift;
            bool needAltGr          = mapping.needs_altgr;
            bool ctrlAlreadyActive  = keyStates[0x11] || (modifiers & MOD_CTRL) != 0;
            bool altAlreadyActive   = keyStates[0x12] || (modifiers & MOD_ALT) != 0;
            if (needShift && !shiftAlreadyActive)
            {
                pulseKey(0x10);
            }
            if (needAltGr)
            {
                if (!ctrlAlreadyActive)
                {
                    pulseKey(0x11);
                }
                if (!altAlreadyActive)
                {
                    pulseKey(0x12);
                }
            }
            updateTextPreview(label);
            pulseKey(vk);
            return;
        }
    }

    if (needsKeyEvent)
    {
        vita_log::warning("[KeyboardOverlay] No VK mapping for modified key: %s", label.c_str());
        clearModifiers();
        return;
    }

    if (!send_utf8_codepoint(codepoint))
    {
        vita_log::info("[KeyboardOverlay] UTF-8 send failed for key: %s", label.c_str());
        return;
    }
    updateTextPreview(label);
    clearModifiers();
}

// Custom drawn: key grid
void KeyboardOverlay::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    BaseOverlay::draw(vg, x, y, width, height, style, ctx);

    if (keyRows.empty())
        return;

    if (fullscreen && showTextPreview)
    {
        const float previewX = panelX + 12.0f;
        const float previewY = panelY + 8.0f;
        const float previewW = panelW - 24.0f;
        const float previewH = previewHeight - 12.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, previewX, previewY, previewW, previewH, 5.0f);
        nvgFillColor(vg, nvgRGBA(8, 10, 14, 230));
        nvgFill(vg);
        nvgStrokeColor(vg, nvgRGBA(70, 74, 82, 255));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
        nvgFontSize(vg, 20.0f);
        nvgFontFaceId(vg, 0);
        nvgFillColor(vg, nvgRGBA(230, 230, 230, 255));
        const std::string preview = textPreview.empty() ? "|" : textPreview + "|";
        nvgSave(vg);
        nvgScissor(vg, previewX + 8.0f, previewY, previewW - 16.0f, previewH);
        nvgText(vg, previewX + 10.0f, previewY + 28.0f, preview.c_str(), nullptr);
        nvgRestore(vg);
    }

    if (highlightFrames > 0)
        highlightFrames--;
    if (highlightFrames == 0)
    {
        highlightRow = -1;
        highlightCol = -1;
    }

    float baseKeyW = this->btnW;

    nvgFontSize(vg, 18.0f);
    nvgFontFaceId(vg, 0);
    float startY = this->panelY + this->btnYStart;

    for (size_t row = 0; row < keyRows.size(); ++row)
    {
        const auto& cols = keyRows[row];
        if (cols.empty())
            continue;

        int spacerCount = 0;
        float fixedW    = 0.0f;
        for (const auto& key : cols)
        {
            if (key.action == "Spacer")
            {
                spacerCount++;
            }
            else
            {
                fixedW += baseKeyW * key.widthMult;
            }
        }
        float available = panelW - 2.0f * btnXOffset;
        float margins   = cols.size() > 0 ? (float)(cols.size() - 1) * btnMargin : 0.0f;
        float remaining = available - fixedW - margins;
        float spacerW   = (spacerCount > 0 && remaining > 0.0f) ? (remaining / (float)spacerCount) : 0.0f;
        float startX    = this->panelX + btnXOffset;
        float rowY      = startY + row * (this->btnH + this->btnMargin);
        float keyX      = startX;

        for (size_t c = 0; c < cols.size(); ++c)
        {
            float kw = cols[c].action == "Spacer" ? spacerW : baseKeyW * cols[c].widthMult;
            if (cols[c].action == "Spacer")
            {
                keyX += kw + this->btnMargin;
                continue;
            }

            // Determine if this key is highlighted
            bool isHighlighted = (highlightRow == (int)row && highlightCol == (int)c && highlightFrames > 0) || (pressedActive && pressedRow == (int)row && pressedCol == (int)c);

            // Key background
            nvgBeginPath(vg);
            nvgRoundedRect(vg, keyX, rowY, kw, this->btnH, 5.0f);

            NVGcolor bg;
            if (isHighlighted)
            {
                bg = nvgRGBA(100, 180, 255, (int)(this->localPanelAlpha * 255));
            }
            else if (isModifierLatched(cols[c].action))
            {
                bg = nvgRGBA(100, 140, 220, (int)(this->localPanelAlpha * 255));
            }
            else if (cols[c].action == "VideoFocus")
            {
                bg = nvgRGBA(45, 105, 190, (int)(this->localPanelAlpha * 255));
            }
            else if (cols[c].action == "Close")
            {
                bg = nvgRGBA(180, 50, 50, (int)(this->localPanelAlpha * 255));
            }
            else
            {
                bg = nvgRGBA(40, 44, 52, (int)(this->localPanelAlpha * 255));
            }
            nvgFillColor(vg, bg);
            nvgFill(vg);

            // Border
            nvgStrokeColor(vg, isHighlighted ? nvgRGBA(255, 255, 255, 255) : nvgRGBA(70, 74, 82, 255));
            nvgStrokeWidth(vg, isHighlighted ? 2.0f : 1.0f);
            nvgStroke(vg);

            // Determine display text
            const std::string& label = cols[c].label;
            std::string display      = label;
            if (cols[c].action == "VideoFocus")
            {
                display = showBottomVideo ? "^" : "v";
            }
            // If it is a single-codepoint key and Shift is active, show uppercase
            std::uint32_t displayCodepoint = 0;
            if ((modifiers & MOD_SHIFT) != 0 && decode_single_utf8(label, displayCodepoint))
            {
                const std::uint32_t shifted = apply_shift_to_codepoint(displayCodepoint);
                if (shifted != displayCodepoint)
                {
                    const std::string shiftedDisplay = utf8_from_codepoint(shifted);
                    if (!shiftedDisplay.empty())
                    {
                        display = shiftedDisplay;
                    }
                }
            }

            // Center text in key using nvgTextBounds
            float textBounds[4];
            float fontSz = (cols[c].widthMult >= 1.3f) ? 15.0f : 18.0f;
            nvgFontSize(vg, fontSz);
            float textW = nvgTextBounds(vg, 0, 0, display.c_str(), nullptr, textBounds);
            float textH = textBounds[3] - textBounds[1]; // approximate height

            if (textW > kw * 0.9f && fontSz > 12.0f)
            {
                fontSz = 13.0f;
                nvgFontSize(vg, fontSz);
                textW = nvgTextBounds(vg, 0, 0, display.c_str(), nullptr, textBounds);
                textH = textBounds[3] - textBounds[1];
            }
            if (textW > kw * 0.9f && fontSz > 10.0f)
            {
                fontSz = 11.0f;
                nvgFontSize(vg, fontSz);
                textW = nvgTextBounds(vg, 0, 0, display.c_str(), nullptr, textBounds);
                textH = textBounds[3] - textBounds[1];
            }

            float tx = keyX + (kw - textW) * 0.5f;
            float ty = rowY + (this->btnH + textH) * 0.5f;

            nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
            nvgText(vg, tx, ty, display.c_str(), nullptr);

            keyX += kw + this->btnMargin;
        }
    }
}
