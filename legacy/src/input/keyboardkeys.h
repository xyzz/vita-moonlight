#ifndef KEYBOARDKEYS_H
#define KEYBOARDKEYS_H

#include <wchar.h>

// Estructura para el mapeo carácter → VK + shift
struct CharVKMap {
    wchar_t ch;
    int vk;
    int needs_shift;
};

// Enumeración de layouts soportados
typedef enum {
    KB_LAYOUT_EN_US = 0,
    KB_LAYOUT_ES_ES,
    KB_LAYOUT_ES_LATAM,
    KB_LAYOUT_COUNT
} KeyboardLayout;

// Devuelve el diccionario y el tamaño para el layout seleccionado
const struct CharVKMap* get_char_vk_dict(KeyboardLayout layout, int* out_count);

void keyboardsystem_set_layout(KeyboardLayout layout);

#endif // KEYBOARDKEYS_H
