#pragma once
#include <atomic>

// Cache atómico del modo de render para evitar leer ConfigManager por frame.
// Valores esperados: 0=legacy,1=ffmpeg,2=borealis (fase 1), etc.
// Valor inicial -1 indica "no inicializado".
extern std::atomic<int> g_render_mode_current;

// Establecer nuevo modo (se llama desde settings_tab al cambiar).
void set_render_mode_cached(int v);

// Obtener el valor cacheado (puede ser -1 si no se inicializó).
int get_render_mode_cached();

// Asegura que el cache esté inicializado: si está en -1, lee ConfigManager una vez.
// Devuelve el modo final.
int ensure_render_mode_cached();
