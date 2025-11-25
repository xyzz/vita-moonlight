#pragma once

#include <cwchar>

// Estructura para el mapeo carácter → VK + shift
struct CharVKMap
{
    wchar_t ch;
    int vk;
    int needs_shift;
};

// Enumeración de layouts soportados
enum KeyboardLayout
{
    KB_LAYOUT_EN_US = 0,
    KB_LAYOUT_ES_ES,
    KB_LAYOUT_ES_LATAM,
    KB_LAYOUT_COUNT
};

// Devuelve el diccionario y el tamaño para el layout seleccionado
const struct CharVKMap* get_char_vk_dict(KeyboardLayout layout, int* out_count);

// Permite cambiar layout en tiempo de ejecución
void keyboardsystem_set_layout(KeyboardLayout layout);
