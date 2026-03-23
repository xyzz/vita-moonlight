#pragma once

#include <cwchar>

// Structure for mapping character → VK + shift
struct CharVKMap
{
    wchar_t ch;
    int vk;
    int needs_shift;
};

// Enumeration of supported layouts
enum KeyboardLayout
{
    KB_LAYOUT_EN_US = 0,
    KB_LAYOUT_ES_ES,
    KB_LAYOUT_ES_LATAM,
    KB_LAYOUT_COUNT
};

// Returns the dictionary and size for the selected layout
const struct CharVKMap* get_char_vk_dict(KeyboardLayout layout, int* out_count);

// Allows you to change layout at run time
void keyboardsystem_set_layout(KeyboardLayout layout);
