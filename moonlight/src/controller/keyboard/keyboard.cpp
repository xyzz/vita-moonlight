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
    panelH = 240.0f;
    panelX = 0.0f;
    panelY = 0.0f;
    loadCss(cssPath);
    initDefaultLayout();

    // Register recognizer for taps that maps to the key grid
    // Tap gesture to detect keys in grid
    this->addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* sound)
        {
        if (status.state == brls::GestureState::END) {
            float tapX = status.position.x;
            float tapY = status.position.y;

            brls::Logger::info("[KeyboardOverlay] Tap detected at X={}, Y={}", tapX, tapY);
            brls::Logger::info("[KeyboardOverlay] Panel rect: X={}, Y={}, W={}, H={}", panelX, panelY, panelW, panelH);

            // Calculate base Y
            float startY = this->panelY + this->btnYStart;
            for (size_t row = 0; row < keyRows.size(); ++row) {
                float rowY = startY + row * (this->btnH + this->btnMargin);
                if (tapY >= rowY && tapY <= rowY + this->btnH) {
                    // Columna
                    const auto &cols = keyRows[row];
                    size_t colsCount = cols.size();
                    if (colsCount == 0) continue; // Skip empty rows

                    float totalW = colsCount * this->btnW + (colsCount - 1) * this->btnMargin;
                    float startX = this->panelX + (this->panelW - totalW) * 0.5f;

                    for (size_t c = 0; c < colsCount; ++c) {
                        float keyX = startX + c * (this->btnW + this->btnMargin);
                        if (tapX >= keyX && tapX <= keyX + this->btnW) {
                            // Activate key
                            brls::Logger::info("[KeyboardOverlay] Hit key: {}", cols[c]);
                            this->sendKeyByLabel(cols[c]);
                            return;
                        }
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
}

KeyboardOverlay::~KeyboardOverlay()
{
    // Unregister
    if (g_controllerInput && g_controllerInput->getActiveKeyboard() == this)
    {
        g_controllerInput->setActiveKeyboard(nullptr);
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
        return;
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
        // Placeholder: simply log in.
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
        size_t maxCols = 0;
        for (auto& r : keyRows)
            if (r.size() > maxCols)
                maxCols = r.size();
        if (maxCols > 0)
        {
            float usable       = panelW - 2.0f * btnXOffset;
            float computedBtnW = (usable - (float)(maxCols - 1) * btnMargin) / (float)maxCols;
            if (computedBtnW > 8.0f)
                btnW = computedBtnW;
        }

        // Set btnYStart to start some padding inside the panel
        btnYStart = 12.0f;
    }
    catch (...)
    {
    }
}

// Inicializa un layout QWERTY simple
void KeyboardOverlay::initDefaultLayout()
{
    keyRows.clear();
    // Lowercase QWERTY layout, second row with 'n tilde'
    keyRows.push_back({ "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" });
    keyRows.push_back({ "a", "s", "d", "f", "g", "h", "j", "k", "l", "ñ" });
    keyRows.push_back({ "z", "x", "c", "v", "b", "n", "m" });
    // Function row (Shift, space, enter, delete, close)
    keyRows.push_back({ "Shift", "Space", "Enter", "Backspace", "Close" });
}

void KeyboardOverlay::sendKeyByLabel(const std::string& label)
{
    if (label == "Close")
    {
        // Pop activity to close the keyboard overlay properly
        brls::Application::popActivity();
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
            // Update state for polling

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
    // Prevent BaseOverlay from drawing buttons (we don't use buttonLabels)
    BaseOverlay::draw(vg, x, y, width, height, style, ctx);

    if (keyRows.empty())
        return;

    nvgFontSize(vg, 20.0f);
    nvgFontFaceId(vg, 0);
    float startY = this->panelY + this->btnYStart;
    for (size_t row = 0; row < keyRows.size(); ++row)
    {
        const auto& cols = keyRows[row];
        size_t colsCount = cols.size();
        if (colsCount == 0)
            continue;
        float totalW = colsCount * this->btnW + (colsCount - 1) * this->btnMargin;
        float startX = this->panelX + (this->panelW - totalW) * 0.5f;
        float rowY   = startY + row * (this->btnH + this->btnMargin);
        for (size_t c = 0; c < colsCount; ++c)
        {
            float keyX = startX + c * (this->btnW + this->btnMargin);
            // Fondo
            nvgBeginPath(vg);
            nvgRoundedRect(vg, keyX, rowY, this->btnW, this->btnH, 6.0f);
            NVGcolor bg = nvgRGBA(40, 44, 52, (int)(this->localPanelAlpha * 255));
            nvgFillColor(vg, bg);
            nvgFill(vg);
            // Borde
            nvgStrokeColor(vg, nvgRGBA(80, 80, 80, 255));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
            // Centered text
            nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
            const std::string& label = cols[c];
            std::string display      = label;
            // If it is a 1 character key and Shift is active, show uppercase
            if (label.size() == 1 && this->shiftActive)
            {
                unsigned char ch = (unsigned char)label[0];
                display[0]       = (char)std::toupper(ch);
            }

            // Special color for Shift when active
            if (label == "Shift")
            {
                NVGcolor sbg = this->shiftActive ? nvgRGBA(100, 140, 220, (int)(this->localPanelAlpha * 255)) : nvgRGBA(40, 44, 52, (int)(this->localPanelAlpha * 255));
                nvgFillColor(vg, sbg);
                nvgFill(vg);
                // Borde
                nvgStrokeColor(vg, nvgRGBA(120, 120, 160, 255));
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
                // Texto
                nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            }

            float tx = keyX + 12.0f;
            float ty = rowY + this->btnH * 0.5f + 6.0f;
            nvgText(vg, tx, ty, display.c_str(), nullptr);
        }
    }
}

// Get current keyboard state for polling
KeyboardState KeyboardOverlay::getKeyboardState() const
{
    KeyboardState state;
    memcpy(state.keys, keyStates, sizeof(keyStates));
    return state;
}
