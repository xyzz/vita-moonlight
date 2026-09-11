#include "controller/keyboard/keyboardloader.hpp"

#include <borealis/extern/tinyxml2/tinyxml2.h>
#include <dirent.h>

#include <cctype>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

#include "controller/keyboard/keyboard_utf8.hpp"

struct CharVKMap
{
    wchar_t ch;
    int vk;
    int needs_shift;
    int needs_altgr = 0;
};

static std::vector<CharVKMap> g_layout_dicts[KB_LAYOUT_COUNT];
static bool g_layout_loaded[KB_LAYOUT_COUNT] = { false };

static std::map<std::string, KeyboardLayout> filename_to_layout = {
    { "en-global.xml", KB_LAYOUT_EN_US },
    { "es-es.xml", KB_LAYOUT_ES_ES },
    { "es-mx.xml", KB_LAYOUT_ES_LATAM }
};

static int hex_to_int(const char* hex)
{
    int result = 0;
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
    {
        hex += 2;
    }
    while (*hex)
    {
        char c = *hex++;
        if (c >= '0' && c <= '9')
            result = result * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f')
            result = result * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            result = result * 16 + (c - 'A' + 10);
    }
    return result;
}

static int vk_name_to_code(const char* vk_name)
{
    if (!vk_name)
        return 0;

    static std::map<std::string, int> special_keys = {
        { "VK_ESCAPE", 0x1B }, { "VK_BACK", 0x08 }, { "VK_TAB", 0x09 }, { "VK_RETURN", 0x0D },
        { "VK_SPACE", 0x20 }, { "VK_LSHIFT", 0x10 }, { "VK_RSHIFT", 0x10 },
        { "VK_LCONTROL", 0x11 }, { "VK_RCONTROL", 0x11 }, { "VK_LMENU", 0x12 }, { "VK_RMENU", 0x12 },
        { "VK_CAPITAL", 0x14 }, { "VK_OEM_1", 0xBA }, { "VK_OEM_2", 0xBF }, { "VK_OEM_3", 0xC0 },
        { "VK_OEM_4", 0xDB }, { "VK_OEM_5", 0xDC }, { "VK_OEM_6", 0xDD }, { "VK_OEM_7", 0xDE },
        { "VK_OEM_PLUS", 0xBB }, { "VK_OEM_MINUS", 0xBD }, { "VK_OEM_COMMA", 0xBC },
        { "VK_OEM_PERIOD", 0xBE }, { "VK_OEM_102", 0xE2 },
        { "VK_NUMLOCK", 0x90 }, { "VK_SCROLL", 0x91 }, { "VK_CLEAR", 0x0C }, { "VK_SNAPSHOT", 0x2A },
        { "VK_LWIN", 0x5B }, { "VK_RWIN", 0x5C }, { "VK_APPS", 0x5D }, { "VK_SLEEP", 0x5F },
        { "VK_PAUSE", 0x13 }, { "VK_CANCEL", 0x03 }, { "VK_HELP", 0x2F },
        { "VK_MEDIA_PREV_TRACK", 0x10 }, { "VK_MEDIA_NEXT_TRACK", 0x19 },
        { "VK_MEDIA_PLAY_PAUSE", 0x22 }, { "VK_MEDIA_STOP", 0x24 },
        { "VK_VOLUME_MUTE", 0x20 }, { "VK_VOLUME_DOWN", 0x2E }, { "VK_VOLUME_UP", 0x30 },
        { "VK_BROWSER_HOME", 0x32 }, { "VK_BROWSER_SEARCH", 0x65 }, { "VK_BROWSER_FAVORITES", 0x66 },
        { "VK_BROWSER_REFRESH", 0x67 }, { "VK_BROWSER_STOP", 0x68 }, { "VK_BROWSER_FORWARD", 0x69 },
        { "VK_BROWSER_BACK", 0x6A }, { "VK_LAUNCH_APP1", 0x6B }, { "VK_LAUNCH_APP2", 0x21 },
        { "VK_LAUNCH_MAIL", 0x6C }, { "VK_LAUNCH_MEDIA_SELECT", 0x6D }
    };

    auto it = special_keys.find(vk_name);
    if (it != special_keys.end())
        return it->second;

    if (strncmp(vk_name, "VK_", 3) == 0 && strlen(vk_name) == 4 && vk_name[3] >= '0' && vk_name[3] <= '9')
    {
        return 0x30 + (vk_name[3] - '0');
    }

    if (strncmp(vk_name, "VK_", 3) == 0 && strlen(vk_name) == 4 && vk_name[3] >= 'A' && vk_name[3] <= 'Z')
    {
        return 0x41 + (vk_name[3] - 'A');
    }

    if (strncmp(vk_name, "VK_F", 3) == 0 && strlen(vk_name) >= 4)
    {
        int num = atoi(vk_name + 3);
        if (num >= 1 && num <= 12)
        {
            return 0x70 + num - 1;
        }
    }

    if (strncmp(vk_name, "VK_NUMPAD", 9) == 0 && strlen(vk_name) == 10)
    {
        int num = vk_name[9] - '0';
        if (num >= 0 && num <= 9)
        {
            return 0x60 + num;
        }
    }

    static std::map<std::string, int> numpad_ops = {
        { "VK_MULTIPLY", 0x6A }, { "VK_DIVIDE", 0x6F }, { "VK_ADD", 0x6B }, { "VK_SUBTRACT", 0x6D }
    };
    it = numpad_ops.find(vk_name);
    if (it != numpad_ops.end())
        return it->second;

    static std::map<std::string, int> nav_keys = {
        { "VK_PRIOR", 0x21 }, { "VK_NEXT", 0x22 }, { "VK_END", 0x23 }, { "VK_HOME", 0x24 },
        { "VK_LEFT", 0x25 }, { "VK_UP", 0x26 }, { "VK_RIGHT", 0x27 }, { "VK_DOWN", 0x28 },
        { "VK_INSERT", 0x2D }, { "VK_DELETE", 0x2E }
    };
    it = nav_keys.find(vk_name);
    if (it != nav_keys.end())
        return it->second;

    return 0;
}

bool load_layout_from_xml(KeyboardLayout layout, const char* filename)
{
    tinyxml2::XMLDocument doc;
    std::string path = "app0:resources/keyboard/";
    path += filename;

    if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
    {
        return false;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("KeyboardLayout");
    if (!root)
        return false;

    tinyxml2::XMLElement* physical_keys = root->FirstChildElement("PhysicalKeys");
    if (!physical_keys)
        return false;

    std::vector<CharVKMap> dict;

    for (tinyxml2::XMLElement* pk = physical_keys->FirstChildElement("PK"); pk; pk = pk->NextSiblingElement("PK"))
    {
        const char* vk_name = pk->Attribute("VK");
        if (!vk_name)
            continue;

        int vk = vk_name_to_code(vk_name);
        if (vk == 0)
            continue;

        for (tinyxml2::XMLElement* result = pk->FirstChildElement("Result"); result; result = result->NextSiblingElement("Result"))
        {
            const char* with           = result->Attribute("With");
            const char* text_attr      = result->Attribute("Text");
            const char* codepoint_attr = result->Attribute("TextCodepoints");

            bool needs_shift = false;
            bool needs_altgr = false;

            if (with)
            {
                std::string with_str = with;
                // Caps Lock is keyboard state, not a modifier for a single character.
                if (with_str.find("VK_CAPITAL") != std::string::npos)
                    continue;
                if (with_str.find("VK_SHIFT") != std::string::npos)
                    needs_shift = true;
                if (with_str.find("VK_CONTROL") != std::string::npos && with_str.find("VK_MENU") != std::string::npos)
                    needs_altgr = true;
            }

            wchar_t ch = L'\0';
            if (text_attr)
            {
                std::uint32_t codepoint = 0;
                if (decode_single_utf8(text_attr, codepoint))
                {
                    ch = static_cast<wchar_t>(codepoint);
                }
            }
            else if (codepoint_attr)
            {
                ch = static_cast<wchar_t>(hex_to_int(codepoint_attr));
            }

            if (ch != L'\0')
            {
                CharVKMap entry = { ch, vk, needs_shift ? 1 : 0, needs_altgr ? 1 : 0 };
                dict.push_back(entry);
            }
        }
    }

    g_layout_dicts[layout]  = dict;
    g_layout_loaded[layout] = true;
    return true;
}

void auto_load_keyboard_layouts()
{
    for (const auto& [filename, layout] : filename_to_layout)
    {
        if (!g_layout_loaded[layout])
        {
            load_layout_from_xml(layout, filename.c_str());
        }
    }
}

static const CharVKMap* get_loaded_dict(KeyboardLayout layout, int* out_count)
{
    if (g_layout_loaded[layout] && !g_layout_dicts[layout].empty())
    {
        if (out_count)
            *out_count = g_layout_dicts[layout].size();
        return g_layout_dicts[layout].data();
    }
    if (out_count)
        *out_count = 0;
    return nullptr;
}

static bool is_layout_loaded(KeyboardLayout layout)
{
    return g_layout_loaded[layout];
}

static const CharVKMap* get_char_vk_dict(KeyboardLayout layout, int* out_count)
{
    const struct CharVKMap* loaded_dict = get_loaded_dict(layout, out_count);
    if (loaded_dict)
    {
        return loaded_dict;
    }

    if (out_count)
        *out_count = 0;
    return nullptr;
}

bool lookup_vk_mapping(KeyboardLayout layout, std::uint32_t codepoint, VkMapping& out_mapping)
{
    if (codepoint > 0xFFFF)
    {
        return false;
    }

    auto_load_keyboard_layouts();

    int count             = 0;
    const CharVKMap* dict = get_char_vk_dict(layout, &count);
    if (!dict || count == 0)
    {
        return false;
    }

    const wchar_t target = static_cast<wchar_t>(codepoint);
    auto is_numpad_vk    = [](int vk) -> bool
    {
        return (vk >= 0x60 && vk <= 0x6F);
    };
    auto is_oem_vk = [](int vk) -> bool
    {
        return (vk >= 0xBA && vk <= 0xE2);
    };

    bool found     = false;
    int best_score = -1000;
    VkMapping best {};

    for (int i = 0; i < count; ++i)
    {
        if (dict[i].ch != target)
        {
            continue;
        }

        VkMapping candidate;
        candidate.vk          = dict[i].vk;
        candidate.needs_shift = (dict[i].needs_shift != 0);
        candidate.needs_altgr = (dict[i].needs_altgr != 0);

        int score = 0;
        if (!candidate.needs_altgr)
            score += 4;
        if (!candidate.needs_shift)
            score += 2;
        if (is_oem_vk(candidate.vk))
            score += 1;
        if (is_numpad_vk(candidate.vk))
            score -= 2;

        if (!found || score > best_score)
        {
            best       = candidate;
            best_score = score;
            found      = true;
        }
    }

    if (found)
    {
        out_mapping = best;
        return true;
    }

    return false;
}
