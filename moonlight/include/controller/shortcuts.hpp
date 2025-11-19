#pragma once
#include <psp2/ctrl.h>

#include <cstdint>
#include <functional>

// Establecer callback para pausa
void set_pause_callback(const std::function<void()>& cb);

// Devuelve true si se ejecutó un acceso directo y se debe limpiar el input
bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old);

// Ejecuta un acceso directo virtual asociado a un código especial (por ejemplo, pause menu).
bool trigger_virtual_shortcut(std::uint32_t specialKey);
