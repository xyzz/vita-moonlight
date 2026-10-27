#ifndef KEYBOARDKEYS_H
#define KEYBOARDKEYS_H

#include <wchar.h>

// Maps a Unicode character to a virtual key and modifiers.
struct CharVKMap
{
    wchar_t ch;
    int vk;
    int needs_shift;
};

typedef enum
{
    KB_LAYOUT_EN_US = 0,
    KB_LAYOUT_ES_ES,
    KB_LAYOUT_ES_LATAM,
    KB_LAYOUT_COUNT
} KeyboardLayout;

// Returns the selected layout's mapping table and its length.
const struct CharVKMap* get_char_vk_dict(KeyboardLayout layout, int* out_count);

void keyboardsystem_set_layout(KeyboardLayout layout);

#endif // KEYBOARDKEYS_H
