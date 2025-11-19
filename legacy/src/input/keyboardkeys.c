#include "keyboardkeys.h"

// Diccionario para layout EN_US (teclado inglés)
static const struct CharVKMap char_to_vk_dict_en_us[] = {
    // Letras
    {L'a', 0x41, 0}, {L'A', 0x41, 1}, {L'b', 0x42, 0}, {L'B', 0x42, 1}, {L'c', 0x43, 0}, {L'C', 0x43, 1},
    {L'd', 0x44, 0}, {L'D', 0x44, 1}, {L'e', 0x45, 0}, {L'E', 0x45, 1}, {L'f', 0x46, 0}, {L'F', 0x46, 1},
    {L'g', 0x47, 0}, {L'G', 0x47, 1}, {L'h', 0x48, 0}, {L'H', 0x48, 1}, {L'i', 0x49, 0}, {L'I', 0x49, 1},
    {L'j', 0x4A, 0}, {L'J', 0x4A, 1}, {L'k', 0x4B, 0}, {L'K', 0x4B, 1}, {L'l', 0x4C, 0}, {L'L', 0x4C, 1},
    {L'm', 0x4D, 0}, {L'M', 0x4D, 1}, {L'n', 0x4E, 0}, {L'N', 0x4E, 1}, {L'o', 0x4F, 0}, {L'O', 0x4F, 1},
    {L'p', 0x50, 0}, {L'P', 0x50, 1}, {L'q', 0x51, 0}, {L'Q', 0x51, 1}, {L'r', 0x52, 0}, {L'R', 0x52, 1},
    {L's', 0x53, 0}, {L'S', 0x53, 1}, {L't', 0x54, 0}, {L'T', 0x54, 1}, {L'u', 0x55, 0}, {L'U', 0x55, 1},
    {L'v', 0x56, 0}, {L'V', 0x56, 1}, {L'w', 0x57, 0}, {L'W', 0x57, 1}, {L'x', 0x58, 0}, {L'X', 0x58, 1},
    {L'y', 0x59, 0}, {L'Y', 0x59, 1}, {L'z', 0x5A, 0}, {L'Z', 0x5A, 1},
    // Números
    {L'0', 0x30, 0}, {L'1', 0x31, 0}, {L'2', 0x32, 0}, {L'3', 0x33, 0}, {L'4', 0x34, 0},
    {L'5', 0x35, 0}, {L'6', 0x36, 0}, {L'7', 0x37, 0}, {L'8', 0x38, 0}, {L'9', 0x39, 0},
    // Espacio y control
    {L' ', 0x20, 0}, {L'\t', 0x09, 0}, {L'\n', 0x0D, 0}, {L'\r', 0x0D, 0},
    // Símbolos principales (US)
    {L'-', 0xBD, 0}, {L'_', 0xBD, 1}, {L'=', 0xBB, 0}, {L'+', 0xBB, 1},
    {L'[', 0xDB, 0}, {L'{', 0xDB, 1}, {L']', 0xDD, 0}, {L'}', 0xDD, 1},
    {L'\\', 0xDC, 0}, {L'|', 0xDC, 1}, {L';', 0xBA, 0}, {L':', 0xBA, 1},
    {L'\'', 0xDE, 0}, {L'"', 0xDE, 1}, {L',', 0xBC, 0}, {L'<', 0xBC, 1},
    {L'.', 0xBE, 0}, {L'>', 0xBE, 1}, {L'/', 0xBF, 0}, {L'?', 0xBF, 1},
    {L'`', 0xC0, 0}, {L'~', 0xC0, 1},
    // Shifted números (símbolos)
    {L'!', 0x31, 1}, {L'@', 0x32, 1}, {L'#', 0x33, 1}, {L'$', 0x34, 1}, {L'%', 0x35, 1},
    {L'^', 0x36, 1}, {L'&', 0x37, 1}, {L'*', 0x38, 1}, {L'(', 0x39, 1}, {L')', 0x30, 1},
    // Backspace
    {L'\b', 0x08, 0},
};

// Diccionario para layout ES_ES (teclado español España)
static const struct CharVKMap char_to_vk_dict_es_es[] = {
    // Letras
    {L'a', 0x41, 0}, {L'A', 0x41, 1}, {L'b', 0x42, 0}, {L'B', 0x42, 1}, {L'c', 0x43, 0}, {L'C', 0x43, 1},
    {L'd', 0x44, 0}, {L'D', 0x44, 1}, {L'e', 0x45, 0}, {L'E', 0x45, 1}, {L'f', 0x46, 0}, {L'F', 0x46, 1},
    {L'g', 0x47, 0}, {L'G', 0x47, 1}, {L'h', 0x48, 0}, {L'H', 0x48, 1}, {L'i', 0x49, 0}, {L'I', 0x49, 1},
    {L'j', 0x4A, 0}, {L'J', 0x4A, 1}, {L'k', 0x4B, 0}, {L'K', 0x4B, 1}, {L'l', 0x4C, 0}, {L'L', 0x4C, 1},
    {L'm', 0x4D, 0}, {L'M', 0x4D, 1}, {L'n', 0x4E, 0}, {L'N', 0x4E, 1}, {L'o', 0x4F, 0}, {L'O', 0x4F, 1},
    {L'p', 0x50, 0}, {L'P', 0x50, 1}, {L'q', 0x51, 0}, {L'Q', 0x51, 1}, {L'r', 0x52, 0}, {L'R', 0x52, 1},
    {L's', 0x53, 0}, {L'S', 0x53, 1}, {L't', 0x54, 0}, {L'T', 0x54, 1}, {L'u', 0x55, 0}, {L'U', 0x55, 1},
    {L'v', 0x56, 0}, {L'V', 0x56, 1}, {L'w', 0x57, 0}, {L'W', 0x57, 1}, {L'x', 0x58, 0}, {L'X', 0x58, 1},
    {L'y', 0x59, 0}, {L'Y', 0x59, 1}, {L'z', 0x5A, 0}, {L'Z', 0x5A, 1},
    {L'ñ', 0xBA, 0}, {L'Ñ', 0xBA, 1}, // VK_OEM_1
    // Números
    {L'0', 0x30, 0}, {L'1', 0x31, 0}, {L'2', 0x32, 0}, {L'3', 0x33, 0}, {L'4', 0x34, 0},
    {L'5', 0x35, 0}, {L'6', 0x36, 0}, {L'7', 0x37, 0}, {L'8', 0x38, 0}, {L'9', 0x39, 0},
    // Espacio y control
    {L' ', 0x20, 0}, {L'\t', 0x09, 0}, {L'\n', 0x0D, 0}, {L'\r', 0x0D, 0},
    // Símbolos principales (España)
    {L'¡', 0xC0, 0}, {L'¿', 0xBF, 0},
    {L'-', 0xBD, 0}, {L'_', 0xBD, 1}, {L'=', 0xBB, 0}, {L'+', 0xBB, 1},
    {L'`', 0xDE, 0}, {L'^', 0xDE, 1},
    {L'´', 0xC0, 1}, // acento agudo
    {L'ç', 0xBC, 0}, {L'Ç', 0xBC, 1},
    {L'@', 0x32, 1}, {L'#', 0x33, 1}, {L'$', 0x34, 1}, {L'%', 0x35, 1},
    {L'&', 0x36, 1}, {L'/', 0x37, 1}, {L'(', 0x38, 1}, {L')', 0x39, 1},
    {L'"', 0xDE, 1}, {L'\'', 0xDE, 0},
    {L'¡', 0xC0, 0}, {L'!', 0x31, 1},
    {L'?', 0xBF, 1},
    {L'\b', 0x08, 0},
};

// Diccionario para layout ES_LATAM (teclado español latinoamericano)
static const struct CharVKMap char_to_vk_dict_es_latam[] = {
    // Letras
    {L'a', 0x41, 0}, {L'A', 0x41, 1}, {L'b', 0x42, 0}, {L'B', 0x42, 1}, {L'c', 0x43, 0}, {L'C', 0x43, 1},
    {L'd', 0x44, 0}, {L'D', 0x44, 1}, {L'e', 0x45, 0}, {L'E', 0x45, 1}, {L'f', 0x46, 0}, {L'F', 0x46, 1},
    {L'g', 0x47, 0}, {L'G', 0x47, 1}, {L'h', 0x48, 0}, {L'H', 0x48, 1}, {L'i', 0x49, 0}, {L'I', 0x49, 1},
    {L'j', 0x4A, 0}, {L'J', 0x4A, 1}, {L'k', 0x4B, 0}, {L'K', 0x4B, 1}, {L'l', 0x4C, 0}, {L'L', 0x4C, 1},
    {L'm', 0x4D, 0}, {L'M', 0x4D, 1}, {L'n', 0x4E, 0}, {L'N', 0x4E, 1}, {L'o', 0x4F, 0}, {L'O', 0x4F, 1},
    {L'p', 0x50, 0}, {L'P', 0x50, 1}, {L'q', 0x51, 0}, {L'Q', 0x51, 1}, {L'r', 0x52, 0}, {L'R', 0x52, 1},
    {L's', 0x53, 0}, {L'S', 0x53, 1}, {L't', 0x54, 0}, {L'T', 0x54, 1}, {L'u', 0x55, 0}, {L'U', 0x55, 1},
    {L'v', 0x56, 0}, {L'V', 0x56, 1}, {L'w', 0x57, 0}, {L'W', 0x57, 1}, {L'x', 0x58, 0}, {L'X', 0x58, 1},
    {L'y', 0x59, 0}, {L'Y', 0x59, 1}, {L'z', 0x5A, 0}, {L'Z', 0x5A, 1},
    {L'ñ', 0xDB, 0}, {L'Ñ', 0xDB, 1}, // VK_OEM_4
    // Números
    {L'0', 0x30, 0}, {L'1', 0x31, 0}, {L'2', 0x32, 0}, {L'3', 0x33, 0}, {L'4', 0x34, 0},
    {L'5', 0x35, 0}, {L'6', 0x36, 0}, {L'7', 0x37, 0}, {L'8', 0x38, 0}, {L'9', 0x39, 0},
    // Espacio y control
    {L' ', 0x20, 0}, {L'\t', 0x09, 0}, {L'\n', 0x0D, 0}, {L'\r', 0x0D, 0},
    // Símbolos principales (Latino)
    {L'¡', 0xC0, 0}, {L'¿', 0xBF, 0},
    {L'-', 0xBD, 0}, {L'_', 0xBD, 1}, {L'=', 0xBB, 0}, {L'+', 0xBB, 1},
    {L'`', 0xC0, 0}, {L'~', 0xC0, 1},
    {L'´', 0xC0, 1}, // acento agudo
    {L'@', 0x32, 1}, {L'#', 0x33, 1}, {L'$', 0x34, 1}, {L'%', 0x35, 1},
    {L'&', 0x36, 1}, {L'/', 0x37, 1}, {L'(', 0x38, 1}, {L')', 0x39, 1},
    {L'"', 0xDE, 1}, {L'\'', 0xDE, 0},
    {L'!', 0x31, 1},
    {L'?', 0xBF, 1},
    {L'\b', 0x08, 0},
};

const struct CharVKMap* get_char_vk_dict(KeyboardLayout layout, int* out_count) {
    switch (layout) {
        case KB_LAYOUT_EN_US:
            if (out_count) *out_count = sizeof(char_to_vk_dict_en_us)/sizeof(char_to_vk_dict_en_us[0]);
            return char_to_vk_dict_en_us;
        case KB_LAYOUT_ES_ES:
            if (out_count) *out_count = sizeof(char_to_vk_dict_es_es)/sizeof(char_to_vk_dict_es_es[0]);
            return char_to_vk_dict_es_es;
        case KB_LAYOUT_ES_LATAM:
            if (out_count) *out_count = sizeof(char_to_vk_dict_es_latam)/sizeof(char_to_vk_dict_es_latam[0]);
            return char_to_vk_dict_es_latam;
        default:
            if (out_count) *out_count = 0;
            return NULL;
    }
}
