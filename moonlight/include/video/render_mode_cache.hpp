#pragma once
#include <atomic>

// Atomic cache of render mode to avoid reading ConfigManager per frame.
// Expected values: 0=legacy,1=ffmpeg,2=borealis (phase 1), etc.
// Initial value -1 indicates "uninitialized".
extern std::atomic<int> g_render_mode_current;

// Set new mode (called from settings_tab on change).
void set_render_mode_cached(int v);

// Get the cached value (can be -1 if it was not initialized).
int get_render_mode_cached();

// Ensures that the cache is initialized: if it is -1, it reads ConfigManager once.
// Returns the final mode.
int ensure_render_mode_cached();
