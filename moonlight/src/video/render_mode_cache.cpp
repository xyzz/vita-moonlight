#include "video/render_mode_cache.hpp"

#include <atomic>

#include "ConfigManager.hpp"

std::atomic<int> g_render_mode_current { -1 };

void set_render_mode_cached(int v)
{
    g_render_mode_current.store(v, std::memory_order_relaxed);
}

int get_render_mode_cached()
{
    return g_render_mode_current.load(std::memory_order_relaxed);
}

int ensure_render_mode_cached()
{
    int cur = g_render_mode_current.load(std::memory_order_relaxed);
    if (cur == -1)
    {
        ConfigManager cfg;
        cfg.load();
        VideoSettings vs = cfg.getVideoSettings();
        g_render_mode_current.store(vs.render_mode, std::memory_order_relaxed);
        return vs.render_mode;
    }
    return cur;
}
