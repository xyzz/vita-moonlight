#pragma once
#include <psp2/ctrl.h>

#include <cstdint>
#include <functional>

void set_pause_callback(const std::function<void()>& cb);

void set_keyboard_callback(const std::function<void()>& cb);

void reload_shortcuts_config();

bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old);

bool trigger_virtual_shortcut(std::uint32_t specialKey);
