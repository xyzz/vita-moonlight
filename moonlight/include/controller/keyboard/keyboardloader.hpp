#pragma once

#include <cstdint>

struct VkMapping
{
    int vk           = 0;
    bool needs_shift = false;
    bool needs_altgr = false;
};

enum KeyboardLayout
{
    KB_LAYOUT_EN_US = 0,
    KB_LAYOUT_ES_ES,
    KB_LAYOUT_ES_LATAM,
    KB_LAYOUT_COUNT
};

void auto_load_keyboard_layouts();

bool lookup_vk_mapping(KeyboardLayout layout, std::uint32_t codepoint, VkMapping& out_mapping);
