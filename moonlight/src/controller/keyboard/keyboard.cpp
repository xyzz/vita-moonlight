#include "keyboard.hpp"

#include <sys/stat.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
// Limelight API for keyboard submission
#include "Limelight.h"

// UTF-8 helpers for direct text input
#include <algorithm>
#include <borealis/core/thread.hpp>
#include <borealis/core/time.hpp>
#include <cstdint>

#include "ConfigManager.hpp"
#include "controller/ControllerInput.hpp"

namespace
{

constexpr std::uint64_t kRepeatDelayUs    = 400000;
constexpr std::uint64_t kRepeatIntervalUs = 70000;

enum ComboModMask : unsigned int
{
    COMBO_MOD_CTRL  = 1u << 0,
    COMBO_MOD_SHIFT = 1u << 1,
    COMBO_MOD_ALT   = 1u << 2,
    COMBO_MOD_WIN   = 1u << 3
};

class KeyTouchRecognizer : public brls::GestureRecognizer
{
  public:
    using Callback = std::function<void(brls::GestureState, const brls::Point&)>;

    explicit KeyTouchRecognizer(Callback cb)
        : callback(std::move(cb))
    {
    }

    brls::GestureState recognitionLoop(brls::TouchState touch, brls::MouseState mouse, brls::View* view, brls::Sound* soundToPlay) override
    {
        (void)view;
        (void)soundToPlay;
        brls::TouchPhase phase = touch.phase;
        brls::Point position   = touch.position;
        if (phase == brls::TouchPhase::NONE)
        {
            position = mouse.position;
            phase    = mouse.leftButton;
        }

        if (!enabled || phase == brls::TouchPhase::NONE)
        {
            state = brls::GestureState::FAILED;
            return state;
        }

        switch (phase)
        {
            case brls::TouchPhase::START:
                state = brls::GestureState::START;
                break;
            case brls::TouchPhase::STAY:
                state = brls::GestureState::STAY;
                break;
            case brls::TouchPhase::END:
                state = brls::GestureState::END;
                break;
            case brls::TouchPhase::NONE:
                state = brls::GestureState::FAILED;
                break;
        }

        callback(state, position);
        return state;
    }

  private:
    Callback callback;
};

bool decode_single_utf8(const std::string& text, std::uint32_t& outCodepoint)
{
    if (text.empty())
    {
        return false;
    }

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t len      = text.size();

    if (bytes[0] <= 0x7F)
    {
        if (len != 1)
        {
            return false;
        }
        outCodepoint = bytes[0];
        return true;
    }

    if ((bytes[0] & 0xE0) == 0xC0)
    {
        if (len != 2 || (bytes[1] & 0xC0) != 0x80)
        {
            return false;
        }
        outCodepoint = ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
        return true;
    }

    if ((bytes[0] & 0xF0) == 0xE0)
    {
        if (len != 3 || (bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80)
        {
            return false;
        }
        outCodepoint = ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
        return true;
    }

    if ((bytes[0] & 0xF8) == 0xF0)
    {
        if (len != 4 || (bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80 || (bytes[3] & 0xC0) != 0x80)
        {
            return false;
        }
        outCodepoint = ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) | ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
        return true;
    }

    return false;
}

bool encode_utf8_codepoint(std::uint32_t codepoint, char out[4], int& outLen)
{
    outLen = 0;
    if (codepoint <= 0x7F)
    {
        out[0] = static_cast<char>(codepoint);
        outLen = 1;
        return true;
    }
    if (codepoint <= 0x7FF)
    {
        out[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        out[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        outLen = 2;
        return true;
    }
    if (codepoint <= 0xFFFF)
    {
        out[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        out[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        outLen = 3;
        return true;
    }
    if (codepoint <= 0x10FFFF)
    {
        out[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        out[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        outLen = 4;
        return true;
    }
    return false;
}

bool send_utf8_codepoint(std::uint32_t codepoint)
{
    char buf[4] = { 0 };
    int len     = 0;
    if (!encode_utf8_codepoint(codepoint, buf, len))
    {
        return false;
    }
    return LiSendUtf8TextEvent(buf, static_cast<unsigned int>(len)) == 0;
}

std::uint32_t apply_shift_to_codepoint(std::uint32_t codepoint)
{
    if (codepoint >= 'a' && codepoint <= 'z')
    {
        return codepoint - 'a' + 'A';
    }
    if (codepoint == 0x00F1)
    {
        return 0x00D1;
    }
    return codepoint;
}

std::string utf8_from_codepoint(std::uint32_t codepoint)
{
    char buf[4] = { 0 };
    int len     = 0;
    if (!encode_utf8_codepoint(codepoint, buf, len))
    {
        return std::string();
    }
    return std::string(buf, static_cast<std::size_t>(len));
}

} // namespace

static bool file_exists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

KeyboardOverlay::KeyboardOverlay(const std::string& cssPath)
    : cssPath(cssPath)
{
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
        showNumbersRow         = settings.keyboard_numbers_row;
        showArrowKeys          = settings.keyboard_show_arrows;
    }
    initDefaultLayout();

    // Register recognizer for touch press/hold
    auto* touchRecognizer = new KeyTouchRecognizer([this](brls::GestureState state, const brls::Point& position)
        {
        const float tapX = position.x;
        const float tapY = position.y;

        const bool overlayActive = comboModeActive && comboOverlayVisible;
        const bool inOverlay = overlayActive &&
            (tapX >= comboOverlayX && tapX <= comboOverlayX + comboOverlayW &&
             tapY >= comboOverlayY && tapY <= comboOverlayY + comboOverlayH);
        const bool inClear = overlayActive &&
            (tapX >= comboClearX && tapX <= comboClearX + comboClearW &&
             tapY >= comboClearY && tapY <= comboClearY + comboClearH);
        const bool inPanel =
            (tapX >= panelX && tapX <= panelX + panelW &&
             tapY >= panelY && tapY <= panelY + panelH);

        if (!inPanel && !inOverlay) {
            return;
        }

        if (state == brls::GestureState::START) {
            if ((inClear || inOverlay) && comboHasKey) {
                if (comboKeyVk >= 0) {
                    std::vector<int> mods;
                    if (comboMods & COMBO_MOD_CTRL) mods.push_back(0x11);
                    if (comboMods & COMBO_MOD_SHIFT) mods.push_back(0x10);
                    if (comboMods & COMBO_MOD_ALT) mods.push_back(0x12);
                    if (comboMods & COMBO_MOD_WIN) mods.push_back(0x5B);

                    for (int vk : mods) {
                        LiSendKeyboardEvent((short)vk, KEY_ACTION_DOWN, 0);
                    }
                    LiSendKeyboardEvent((short)comboKeyVk, KEY_ACTION_DOWN, 0);
                    brls::delay(20, [mods, key = comboKeyVk]() {
                        LiSendKeyboardEvent((short)key, KEY_ACTION_UP, 0);
                        for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
                            LiSendKeyboardEvent((short)*it, KEY_ACTION_UP, 0);
                        }
                    });
                }
                clearComboSelection();
                comboOverlayVisible = false;
                comboModeActive = false;
                rebuildLayout();
                return;
            }

            int row = -1;
            int col = -1;
            if (hitTestKey(tapX, tapY, row, col)) {
                pressedRow = row;
                pressedCol = col;
                pressedActive = true;

                highlightRow = row;
                highlightCol = col;
                highlightFrames = 6;

                sendKeyByLabel(keyRows[row][col].action);
                startRepeat(keyRows[row][col].action, row, col);
            }
        } else if (state == brls::GestureState::STAY) {
            if (!pressedActive) return;
            int row = -1;
            int col = -1;
            if (!hitTestKey(tapX, tapY, row, col) || row != pressedRow || col != pressedCol) {
                stopRepeat();
                pressedActive = false;
                pressedRow = -1;
                pressedCol = -1;
            }
        } else if (state == brls::GestureState::END || state == brls::GestureState::FAILED || state == brls::GestureState::INTERRUPTED) {
            stopRepeat();
            pressedActive = false;
            pressedRow = -1;
            pressedCol = -1;
        } });
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
}

void KeyboardOverlay::close()
{
    closePending = true;
}

bool KeyboardOverlay::isOpen() const
{
    return visible;
}

KeyboardState KeyboardOverlay::getKeyboardState() const
{
    KeyboardState state;
    memcpy(state.keys, keyStates, sizeof(keyStates));
    return state;
}

void KeyboardOverlay::update()
{
    if (closePending)
    {
        closePending = false;
        brls::sync([this]()
            {
            stopRepeat();
            pressedActive = false;
            pressedRow = -1;
            pressedCol = -1;
            hide();
            visible = false;
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
        brls::Logger::info("[KeyboardOverlay] background-image specified: {}", it->second);
    }

    // Adjust panel so that it fills the entire width and is at the bottom
    try
    {
        float screenW = (float)this->getWidth();
        float screenH = (float)this->getHeight();
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
        float maxPanelH = screenH * 0.5f;
        if (panelH > maxPanelH)
        {
            panelH = maxPanelH;
        }
        panelW = screenW;
        panelX = 0.0f;
        // Place at the bottom
        panelY = std::max(0.0f, screenH - panelH);

        recalculateKeyMetrics();
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

    keyRows.push_back({ KeyDef("Combo", "ComboToggle", 1.2f),
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

    // Combo modifiers row
    if (comboModeActive)
    {
        keyRows.push_back({ KeyDef("Ctrl", "Ctrl", 1.2f),
            KeyDef("Alt", "Alt", 1.2f),
            KeyDef("Win", "Win", 1.2f),
            KeyDef("Esc", "Escape", 1.2f) });
    }

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

    // Function keys row (ESC, Tab, etc.)
    if (comboModeActive)
    {
        keyRows.push_back({ KeyDef("Spacer", "Spacer"),
            KeyDef("Esc", "Escape"),
            KeyDef("Tab"),
            KeyDef("PgUp", "PageUp"),
            KeyDef("PgDn", "PageDown"),
            KeyDef("Spacer", "Spacer") });
    }

    keyRows.push_back({ KeyDef("ABC", "LetterPage", 1.2f),
        KeyDef("Combo", "ComboToggle", 1.2f),
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

    // Combo modifiers row
    if (comboModeActive)
    {
        keyRows.push_back({ KeyDef("Ctrl", "Ctrl", 1.2f),
            KeyDef("Alt", "Alt", 1.2f),
            KeyDef("Win", "Win", 1.2f) });
    }

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
        KeyDef("Combo", "ComboToggle", 1.2f),
        KeyDef("Spacer", "Spacer", 4.0f),
        KeyDef(spaceLabel, "Space", 3.4f),
        KeyDef("Enter", "Enter", 1.4f),
        KeyDef("X", "Close", 1.0f) });

    // Combo modifiers row
    if (comboModeActive)
    {
        keyRows.push_back({ KeyDef("Ctrl", "Ctrl", 1.2f),
            KeyDef("Alt", "Alt", 1.2f),
            KeyDef("Win", "Win", 1.2f) });
    }

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

    btnYStart = 8.0f;

    if (!keyRows.empty())
    {
        float availableH   = panelH - btnYStart - 6.0f;
        float totalMargins = (float)(keyRows.size() - 1) * btnMargin;
        float maxKeyH      = (availableH - totalMargins) / (float)keyRows.size();
        float targetH      = maxKeyH;
        if (baseBtnH > 0.0f && targetH > baseBtnH)
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
    if (action == "Close" || action == "SymPage" || action == "LetterPage" || action == "FnPage" || action == "ComboToggle" || action == "Shift" || action == "Ctrl" || action == "Alt" || action == "Win")
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
    if (comboModeActive || !isRepeatableAction(action))
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

void KeyboardOverlay::clearComboSelection()
{
    comboMods = 0;
    comboKeyLabel.clear();
    comboKeyVk  = -1;
    comboHasKey = false;
}

bool KeyboardOverlay::isSpacerAction(const std::string& action) const
{
    return action == "Spacer";
}

void KeyboardOverlay::setComboMode(bool enabled)
{
    comboModeActive = enabled;
    clearComboSelection();
    shiftActive     = false;
    keyStates[0x10] = false;
    stopRepeat();
    rebuildLayout();
}

std::string KeyboardOverlay::formatComboLabel() const
{
    std::vector<std::string> parts;
    if (comboMods & COMBO_MOD_CTRL)
        parts.push_back("Ctrl");
    if (comboMods & COMBO_MOD_SHIFT)
        parts.push_back("Shift");
    if (comboMods & COMBO_MOD_ALT)
        parts.push_back("Alt");
    if (comboMods & COMBO_MOD_WIN)
        parts.push_back("Win");

    std::string keyLabel = comboKeyLabel;
    if (!keyLabel.empty() && (comboMods & COMBO_MOD_SHIFT))
    {
        std::uint32_t cp = 0;
        if (decode_single_utf8(keyLabel, cp))
        {
            std::uint32_t shifted    = apply_shift_to_codepoint(cp);
            std::string shiftedLabel = utf8_from_codepoint(shifted);
            if (!shiftedLabel.empty())
            {
                keyLabel = shiftedLabel;
            }
        }
    }

    if (!keyLabel.empty())
    {
        parts.push_back(keyLabel);
    }

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        out += parts[i];
        if (i + 1 < parts.size())
            out += " + ";
    }
    return out;
}

bool KeyboardOverlay::isModifierAction(const std::string& action) const
{
    return action == "Ctrl" || action == "Shift" || action == "Alt" || action == "Win";
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

void KeyboardOverlay::sendKeyByLabel(const std::string& label)
{
    // If the Combo toggle button is pressed, enable/disable combo mode
    if (label == "ComboToggle")
    {
        setComboMode(!comboModeActive);
        comboOverlayVisible = comboModeActive; // show overlay when enabling
        return;
    }
    if (label == "Close")
    {
        if (comboModeActive)
        {
            clearComboSelection();
            comboOverlayVisible = false;
            comboModeActive     = false;
        }
        // Defer close so we don't destroy the overlay from the touch callback
        closePending = true;
        return;
    }

    if (label == "Spacer")
    {
        return;
    }

    if (label == "SymPage")
    {
        // Switch to symbols layout
        initSymbolLayout();
        return;
    }

    if (label == "LetterPage")
    {
        // Switch back to letters layout
        initDefaultLayout();
        return;
    }

    if (label == "FnPage")
    {
        // Switch to function keys layout
        initFnLayout();
        return;
    }

    if (label == "Shift")
    {
        if (comboModeActive)
        {
            // Toggle shift modifier for combo
            if (comboMods & COMBO_MOD_SHIFT)
                comboMods &= ~COMBO_MOD_SHIFT;
            else
                comboMods |= COMBO_MOD_SHIFT;
            comboOverlayVisible = true;
            return;
        }
        // Toggle persistent shift (uppercase)
        this->shiftActive = !this->shiftActive;
        // Update Shift VK state for polling
        keyStates[0x10] = this->shiftActive;
        // Shift acts as a toggle, so we don't auto-release it
        return;
    }

    if (label == "Ctrl")
    {
        if (comboModeActive)
        {
            if (comboMods & COMBO_MOD_CTRL)
                comboMods &= ~COMBO_MOD_CTRL;
            else
                comboMods |= COMBO_MOD_CTRL;
            comboOverlayVisible = true;
            return;
        }
        // Ctrl = VK 0x11
        keyStates[0x11] = true;
        brls::delay(50, [this]()
            { keyStates[0x11] = false; });
        return;
    }

    if (label == "Alt")
    {
        if (comboModeActive)
        {
            if (comboMods & COMBO_MOD_ALT)
                comboMods &= ~COMBO_MOD_ALT;
            else
                comboMods |= COMBO_MOD_ALT;
            comboOverlayVisible = true;
            return;
        }
        // Alt = VK 0x12
        keyStates[0x12] = true;
        brls::delay(50, [this]()
            { keyStates[0x12] = false; });
        return;
    }

    if (label == "Win")
    {
        if (comboModeActive)
        {
            if (comboMods & COMBO_MOD_WIN)
                comboMods &= ~COMBO_MOD_WIN;
            else
                comboMods |= COMBO_MOD_WIN;
            comboOverlayVisible = true;
            return;
        }
        // Win = VK 0x5B
        keyStates[0x5B] = true;
        brls::delay(50, [this]()
            { keyStates[0x5B] = false; });
        return;
    }

    auto trySelectComboKey = [this](const std::string& action) -> bool
    {
        if (!comboModeActive || isModifierAction(action))
        {
            return false;
        }
        int vk = -1;
        if (mapActionToVk(action, vk))
        {
            comboKeyLabel       = action;
            comboKeyVk          = vk;
            comboHasKey         = true;
            comboOverlayVisible = true;
            return true;
        }
        return false;
    };

    if (label == "Tab")
    {
        if (trySelectComboKey(label))
        {
            return;
        }
        // Tab = VK 0x09
        keyStates[0x09] = true;
        brls::delay(50, [this]()
            { keyStates[0x09] = false; });
        return;
    }

    if (label == "Escape")
    {
        if (trySelectComboKey(label))
        {
            return;
        }
        // Escape = VK 0x1B
        keyStates[0x1B] = true;
        brls::delay(50, [this]()
            { keyStates[0x1B] = false; });
        return;
    }

    if (label == "Space")
    {
        if (trySelectComboKey(label))
        {
            return;
        }
        // Space = VK 0x20
        keyStates[0x20] = true;
        brls::delay(50, [this]()
            { keyStates[0x20] = false; });
        return;
    }
    if (label == "Enter")
    {
        if (trySelectComboKey(label))
        {
            return;
        }
        // Enter = VK 0x0D
        keyStates[0x0D] = true;
        brls::delay(50, [this]()
            { keyStates[0x0D] = false; });
        return;
    }
    if (label == "Backspace")
    {
        if (trySelectComboKey(label))
        {
            return;
        }
        // Backspace = VK 0x08
        keyStates[0x08] = true;
        brls::delay(50, [this]()
            { keyStates[0x08] = false; });
        return;
    }

    // Non-text keys (arrows, function keys, etc.)
    std::uint32_t codepoint = 0;
    if (!decode_single_utf8(label, codepoint))
    {
        int vk = -1;
        if (mapActionToVk(label, vk))
        {
            if (comboModeActive && !isModifierAction(label))
            {
                comboKeyLabel       = label;
                comboKeyVk          = vk;
                comboHasKey         = true;
                comboOverlayVisible = true;
                return;
            }
            keyStates[vk] = true;
            brls::delay(50, [this, vk]()
                { keyStates[vk] = false; });
            return;
        }
        return;
    }

    if (comboModeActive)
    {
        // In combo mode, selecting a non-modifier key sets the combo target
        if (!isModifierAction(label))
        {
            comboKeyLabel       = label;
            comboHasKey         = true;
            comboOverlayVisible = true;
            int vk              = -1;
            if (mapActionToVk(label, vk))
                comboKeyVk = vk;
            else
                comboKeyVk = -1;
        }
        return;
    }

    if (this->shiftActive)
    {
        codepoint = apply_shift_to_codepoint(codepoint);
    }

    if (!send_utf8_codepoint(codepoint))
    {
        brls::Logger::info("[KeyboardOverlay] UTF-8 send failed for key: {}", label);
    }
}

// Custom drawn: key grid
void KeyboardOverlay::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    BaseOverlay::draw(vg, x, y, width, height, style, ctx);

    if (keyRows.empty())
        return;

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
            bool isHighlighted = (highlightRow == (int)row && highlightCol == (int)c && highlightFrames > 0);

            // Key background
            nvgBeginPath(vg);
            nvgRoundedRect(vg, keyX, rowY, kw, this->btnH, 5.0f);

            NVGcolor bg;
            if (isHighlighted)
            {
                bg = nvgRGBA(100, 180, 255, (int)(this->localPanelAlpha * 255));
            }
            else if (comboModeActive && (isModifierAction(cols[c].action) || cols[c].action == "ComboToggle"))
            {
                // Highlight modifier keys in combo mode if they are selected
                if ((cols[c].action == "Shift" && (comboMods & COMBO_MOD_SHIFT)) || (cols[c].action == "Ctrl" && (comboMods & COMBO_MOD_CTRL)) || (cols[c].action == "Alt" && (comboMods & COMBO_MOD_ALT)) || (cols[c].action == "Win" && (comboMods & COMBO_MOD_WIN)) || cols[c].action == "ComboToggle")
                {
                    bg = nvgRGBA(100, 140, 220, (int)(this->localPanelAlpha * 255));
                }
                else
                {
                    bg = nvgRGBA(40, 44, 52, (int)(this->localPanelAlpha * 255));
                }
            }
            else if (cols[c].action == "Shift" && this->shiftActive)
            {
                bg = nvgRGBA(100, 140, 220, (int)(this->localPanelAlpha * 255));
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
            // If it is a single-codepoint key and Shift is active, show uppercase
            std::uint32_t displayCodepoint = 0;
            if (this->shiftActive && decode_single_utf8(label, displayCodepoint))
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

    // Draw combo overlay in the upper-right area if active
    if (comboModeActive && comboOverlayVisible)
    {
        // Compute overlay geometry: top-right, within upper half of screen
        float screenW = (float)this->getWidth();
        float screenH = (float)this->getHeight();
        comboOverlayW = std::min(panelW * 0.45f, screenW * 0.6f);
        comboOverlayH = 48.0f;
        comboOverlayX = panelX + panelW - comboOverlayW - 12.0f;
        comboOverlayY = std::max(12.0f, panelY * 0.5f - comboOverlayH * 0.5f);

        // Background
        nvgBeginPath(vg);
        nvgRoundedRect(vg, comboOverlayX, comboOverlayY, comboOverlayW, comboOverlayH, 6.0f);
        nvgFillColor(vg, nvgRGBA(30, 34, 42, (int)(this->localPanelAlpha * 230)));
        nvgFill(vg);
        nvgStrokeColor(vg, nvgRGBA(80, 84, 92, 200));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        // Send button on the right side of overlay
        comboClearW = comboClearH = comboOverlayH - 12.0f;
        comboClearX               = comboOverlayX + comboOverlayW - comboClearW - 8.0f;
        comboClearY               = comboOverlayY + 6.0f;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, comboClearX, comboClearY, comboClearW, comboClearH, 4.0f);
        nvgFillColor(vg, nvgRGBA(180, 60, 60, 220));
        nvgFill(vg);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 220));
        // Draw send arrow
        nvgFontSize(vg, 18.0f);
        float xbounds[4];
        const char* xtext = "->";
        float xw          = nvgTextBounds(vg, 0, 0, xtext, nullptr, xbounds);
        float xtx         = comboClearX + (comboClearW - xw) * 0.5f;
        float xty         = comboClearY + (comboClearH + (xbounds[3] - xbounds[1])) * 0.5f;
        nvgText(vg, xtx, xty, xtext, nullptr);

        // Combo label on left area
        std::string comboText = formatComboLabel();
        if (comboText.empty())
            comboText = "Select combo";
        nvgFontSize(vg, 16.0f);
        float tb[4];
        float tw  = nvgTextBounds(vg, 0, 0, comboText.c_str(), nullptr, tb);
        float ttx = comboOverlayX + 12.0f;
        float tty = comboOverlayY + (comboOverlayH + (tb[3] - tb[1])) * 0.5f;
        nvgFillColor(vg, nvgRGBA(220, 220, 220, 230));
        nvgText(vg, ttx, tty, comboText.c_str(), nullptr);
    }
}
