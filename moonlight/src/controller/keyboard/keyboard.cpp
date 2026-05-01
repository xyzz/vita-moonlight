#include "keyboard.hpp"

#include <sys/stat.h>

#include <fstream>
#include <iostream>
#include <sstream>
// Limelight API for keyboard submission
#include "Limelight.h"

// Char mappings -> VK
#include <algorithm>
#include <cctype>
#include <cwchar>

#include "controller/ControllerInput.hpp"
#include "keyboardkeys.hpp"

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
    panelW = 0.0f;
    panelH = 260.0f;
    panelX = 0.0f;
    panelY = 0.0f;
    loadCss(cssPath);
    initDefaultLayout();

    // Register recognizer for taps that maps to the key grid
    this->addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* sound)
        {
        if (status.state == brls::GestureState::END) {
            float tapX = status.position.x;
            float tapY = status.position.y;

            brls::Logger::info("[KeyboardOverlay] Tap detected at X={}, Y={}", tapX, tapY);

            // Calculate base key width from the widest row
            size_t maxCols = 0;
            for (auto &r : keyRows) if (r.size() > maxCols) maxCols = r.size();
            float baseKeyW = this->btnW;

            float startY = this->panelY + this->btnYStart;
            for (size_t row = 0; row < keyRows.size(); ++row) {
                float rowY = startY + row * (this->btnH + this->btnMargin);
                if (tapY >= rowY && tapY <= rowY + this->btnH) {
                    const auto &cols = keyRows[row];
                    if (cols.empty()) continue;

                    // Calculate total row width with variable key widths
                    float totalW = calcRowWidth(cols, baseKeyW);
                    float startX = this->panelX + (this->panelW - totalW) * 0.5f;

                    float keyX = startX;
                    for (size_t c = 0; c < cols.size(); ++c) {
                        float kw = baseKeyW * cols[c].widthMult;
                        if (tapX >= keyX && tapX <= keyX + kw) {
                            // Activate key
                            brls::Logger::info("[KeyboardOverlay] Hit key: {}", cols[c].action);

                            // Visual feedback
                            highlightRow = (int)row;
                            highlightCol = (int)c;
                            highlightFrames = 6; // ~100ms at 60fps

                            this->sendKeyByLabel(cols[c].action);
                            return;
                        }
                        keyX += kw + this->btnMargin;
                    }
                }
            }
            brls::Logger::info("[KeyboardOverlay] Tap missed all keys");
        } }));

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
    visible = true;
}

void KeyboardOverlay::close()
{
    hide();
    visible = false;
    // Pop activity to properly close
    brls::Application::popActivity();
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
        panelW = screenW;
        panelX = 0.0f;
        // Place at the bottom
        panelY = std::max(0.0f, screenH - panelH);

        // Recalculate btnW so that keys fill available width
        // Find the row with the most "units" (sum of widthMult)
        float maxUnits = 0;
        for (auto& r : keyRows)
        {
            float units = 0;
            for (auto& k : r)
                units += k.widthMult;
            if (units > maxUnits)
                maxUnits = units;
        }
        if (maxUnits > 0)
        {
            float usable   = panelW - 2.0f * btnXOffset;
            size_t maxCols = 0;
            for (auto& r : keyRows)
                if (r.size() > maxCols)
                    maxCols = r.size();
            float margins      = (maxCols > 0) ? (float)(maxCols - 1) * btnMargin : 0;
            float computedBtnW = (usable - margins) / maxUnits;
            if (computedBtnW > 8.0f)
                btnW = computedBtnW;
        }

        // Set btnYStart to start some padding inside the panel
        btnYStart = 10.0f;
    }
    catch (...)
    {
    }
}

void KeyboardOverlay::initDefaultLayout()
{
    keyRows.clear();
    layoutPage = 0;

    // Row 0: Numbers
    keyRows.push_back({ KeyDef("1"), KeyDef("2"), KeyDef("3"), KeyDef("4"), KeyDef("5"),
        KeyDef("6"), KeyDef("7"), KeyDef("8"), KeyDef("9"), KeyDef("0") });
    // Row 1: QWERTY top row
    keyRows.push_back({ KeyDef("q"), KeyDef("w"), KeyDef("e"), KeyDef("r"), KeyDef("t"),
        KeyDef("y"), KeyDef("u"), KeyDef("i"), KeyDef("o"), KeyDef("p") });
    // Row 2: Home row
    keyRows.push_back({ KeyDef("a"), KeyDef("s"), KeyDef("d"), KeyDef("f"), KeyDef("g"),
        KeyDef("h"), KeyDef("j"), KeyDef("k"), KeyDef("l") });
    // Row 3: Bottom row with Shift and Backspace
    keyRows.push_back({ KeyDef("Shift", 1.5f),
        KeyDef("z"), KeyDef("x"), KeyDef("c"), KeyDef("v"),
        KeyDef("b"), KeyDef("n"), KeyDef("m"),
        KeyDef("Bksp", "Backspace", 1.5f) });
    // Row 4: Function row
    keyRows.push_back({ KeyDef("?123", "SymPage", 1.3f),
        KeyDef("Ctrl", "Ctrl", 1.0f),
        KeyDef("Space", "Space", 4.0f),
        KeyDef("Enter", "Enter", 1.5f),
        KeyDef("X", "Close", 1.2f) });
}

// Symbol/number layout page
void KeyboardOverlay::initSymbolLayout()
{
    keyRows.clear();
    layoutPage = 1;

    // Row 0: Shifted numbers (symbols)
    keyRows.push_back({ KeyDef("!"), KeyDef("@"), KeyDef("#"), KeyDef("$"), KeyDef("%"),
        KeyDef("^"), KeyDef("&"), KeyDef("*"), KeyDef("("), KeyDef(")") });
    // Row 1: Common symbols
    keyRows.push_back({ KeyDef("-"), KeyDef("_"), KeyDef("="), KeyDef("+"), KeyDef("["),
        KeyDef("]"), KeyDef("{"), KeyDef("}"), KeyDef("|"), KeyDef("\\") });
    // Row 2: More symbols
    keyRows.push_back({ KeyDef(";"), KeyDef(":"), KeyDef("'"), KeyDef("\""), KeyDef(","),
        KeyDef("."), KeyDef("<"), KeyDef(">"), KeyDef("/") });
    // Row 3: Special keys
    keyRows.push_back({ KeyDef("Tab", "Tab", 1.5f),
        KeyDef("~"), KeyDef("`"), KeyDef("?"),
        KeyDef("Esc", "Escape", 1.0f),
        KeyDef("Bksp", "Backspace", 1.5f) });
    // Row 4: Function row
    keyRows.push_back({ KeyDef("ABC", "LetterPage", 1.3f),
        KeyDef("Ctrl", "Ctrl", 1.0f),
        KeyDef("Space", "Space", 4.0f),
        KeyDef("Enter", "Enter", 1.5f),
        KeyDef("X", "Close", 1.2f) });
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

void KeyboardOverlay::sendKeyByLabel(const std::string& label)
{
    if (label == "Close")
    {
        // Pop activity to close the keyboard overlay properly
        visible = false;
        brls::Application::popActivity();
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

    if (label == "Shift")
    {
        // Toggle persistent shift (uppercase)
        this->shiftActive = !this->shiftActive;
        // Update Shift VK state for polling
        keyStates[0x10] = this->shiftActive;
        // Shift acts as a toggle, so we don't auto-release it
        return;
    }

    if (label == "Ctrl")
    {
        // Ctrl = VK 0x11
        keyStates[0x11] = true;
        brls::delay(50, [this]()
            { keyStates[0x11] = false; });
        return;
    }

    if (label == "Tab")
    {
        // Tab = VK 0x09
        keyStates[0x09] = true;
        brls::delay(50, [this]()
            { keyStates[0x09] = false; });
        return;
    }

    if (label == "Escape")
    {
        // Escape = VK 0x1B
        keyStates[0x1B] = true;
        brls::delay(50, [this]()
            { keyStates[0x1B] = false; });
        return;
    }

    if (label == "Space")
    {
        // Space = VK 0x20
        keyStates[0x20] = true;
        brls::delay(50, [this]()
            { keyStates[0x20] = false; });
        return;
    }
    if (label == "Enter")
    {
        // Enter = VK 0x0D
        keyStates[0x0D] = true;
        brls::delay(50, [this]()
            { keyStates[0x0D] = false; });
        return;
    }
    if (label == "Backspace")
    {
        // Backspace = VK 0x08
        keyStates[0x08] = true;
        brls::delay(50, [this]()
            { keyStates[0x08] = false; });
        return;
    }

    // 1 character letters and symbols
    if (label.size() == 1)
    {
        wchar_t wch = 0;
        // Support ASCII basic
        unsigned char c = label[0];
        wch             = (wchar_t)c;

        // Search in the default EN_US dictionary
        int count             = 0;
        const CharVKMap* dict = get_char_vk_dict(KB_LAYOUT_EN_US, &count);
        if (!dict || count == 0)
            return;

        const CharVKMap* found = nullptr;

        // If Shift is active, try to find the capital letter first
        if (this->shiftActive)
        {
            wchar_t up = (wchar_t)std::toupper((unsigned char)c);
            for (int i = 0; i < count; ++i)
            {
                if (dict[i].ch == up)
                {
                    found = &dict[i];
                    break;
                }
            }
        }

        // If not found (or Shift was not active), search by current character
        if (!found)
        {
            for (int i = 0; i < count; ++i)
            {
                if (dict[i].ch == wch)
                {
                    found = &dict[i];
                    break;
                }
            }
        }

        // Try uppercase/lowercase alternative if not found
        if (!found)
        {
            if (isalpha(c))
            {
                wchar_t alt = isupper(c) ? towlower(c) : towupper(c);
                for (int i = 0; i < count; ++i)
                {
                    if (dict[i].ch == alt)
                    {
                        found = &dict[i];
                        break;
                    }
                }
            }
        }

        if (found)
        {
            // Handle shift requirement for this char
            if (found->needs_shift && !this->shiftActive)
            {
                keyStates[0x10] = true; // Momentary shift
            }

            short vk      = (short)found->vk;
            keyStates[vk] = true;

            // Auto-release after short delay to simulate tap
            brls::delay(50, [this, vk, found]()
                {
                keyStates[vk] = false;
                if (found->needs_shift && !this->shiftActive) {
                    keyStates[0x10] = false; // Release momentary shift
                } });

            brls::Logger::info("[KeyboardOverlay] Set VK 0x{:02X} = true (auto-release scheduled)", vk);
        }
        else
        {
            brls::Logger::info("[KeyboardOverlay] char not found in dict: {}", label);
        }
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

        float totalW = calcRowWidth(cols, baseKeyW);
        float startX = this->panelX + (this->panelW - totalW) * 0.5f;
        float rowY   = startY + row * (this->btnH + this->btnMargin);
        float keyX   = startX;

        for (size_t c = 0; c < cols.size(); ++c)
        {
            float kw = baseKeyW * cols[c].widthMult;

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
            // If it is a 1 character key and Shift is active, show uppercase
            if (label.size() == 1 && this->shiftActive && isalpha((unsigned char)label[0]))
            {
                display[0] = (char)std::toupper((unsigned char)label[0]);
            }

            // Center text in key using nvgTextBounds
            float textBounds[4];
            float fontSz = (cols[c].widthMult >= 1.3f) ? 15.0f : 18.0f;
            nvgFontSize(vg, fontSz);
            float textW = nvgTextBounds(vg, 0, 0, display.c_str(), nullptr, textBounds);
            float textH = textBounds[3] - textBounds[1]; // approximate height

            float tx = keyX + (kw - textW) * 0.5f;
            float ty = rowY + (this->btnH + textH) * 0.5f;

            nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
            nvgText(vg, tx, ty, display.c_str(), nullptr);

            keyX += kw + this->btnMargin;
        }
    }
}
