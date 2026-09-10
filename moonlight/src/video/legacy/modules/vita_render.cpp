#include "vita_globals.hpp"

extern "C" void vitavideo_start()
{
    VITA_DEBUG_LOG("[Video] vitavideo_start called");
    active_video_thread.store(true, std::memory_order_release);
    VITA_DEBUG_LOG("[Video] vitavideo_start completado");
}

extern "C" void vitavideo_stop()
{
    VITA_DEBUG_LOG("[Video] vitavideo_stop called");
    active_video_thread.store(false, std::memory_order_release);
    VITA_DEBUG_LOG("[Video] vitavideo_stop completado");
}
