#include "video/runtime_settings.hpp"

#include <atomic>
#include <cstdlib>

#include "ConfigManager.hpp"

namespace
{

std::atomic<bool> motionControls { false };
std::atomic<float> motionX { 1.2f };
std::atomic<float> motionY { 0.8f };
std::atomic<bool> fpsVisible { false };
std::atomic<bool> centerRegion { false };
std::atomic<bool> fullscreenStretch { true };
std::atomic<bool> forceGpuYuv { false };
std::atomic<int> renderer { 0 };
std::atomic<int> pixelFormat { 1 };

}

namespace video_runtime
{

void apply(const VideoSettings& settings)
{
    const char* env = std::getenv("MOONLIGHT_FFMPEG_GPU_YUV");
    forceGpuYuv.store(env && env[0] == '1', std::memory_order_relaxed);
    motionControls.store(settings.enable_motion_controls, std::memory_order_relaxed);
    motionX.store(settings.motion_controls_scalar_x, std::memory_order_relaxed);
    motionY.store(settings.motion_controls_scalar_y, std::memory_order_relaxed);
    fpsVisible.store(settings.show_fps, std::memory_order_relaxed);
    centerRegion.store(settings.center_region_only, std::memory_order_relaxed);
    fullscreenStretch.store(settings.fullscreen, std::memory_order_relaxed);
    renderer.store(settings.render_mode, std::memory_order_relaxed);
    pixelFormat.store(settings.pixel_format_mode, std::memory_order_relaxed);
}

bool motionControlsEnabled() { return motionControls.load(std::memory_order_relaxed); }
float motionScaleX() { return motionX.load(std::memory_order_relaxed); }
float motionScaleY() { return motionY.load(std::memory_order_relaxed); }
bool showFps() { return fpsVisible.load(std::memory_order_relaxed); }
bool centerRegionOnly() { return centerRegion.load(std::memory_order_relaxed); }
bool fullscreen() { return fullscreenStretch.load(std::memory_order_relaxed); }
bool gpuYuvEnabled() { return forceGpuYuv.load(std::memory_order_relaxed) || pixelFormatMode() == 1; }
int renderMode() { return renderer.load(std::memory_order_relaxed); }
int pixelFormatMode() { return pixelFormat.load(std::memory_order_relaxed); }

void setShowFps(bool enabled) { fpsVisible.store(enabled, std::memory_order_relaxed); }
void setMotionControlsEnabled(bool enabled) { motionControls.store(enabled, std::memory_order_relaxed); }
void setMotionScaleX(float scale) { motionX.store(scale, std::memory_order_relaxed); }
void setMotionScaleY(float scale) { motionY.store(scale, std::memory_order_relaxed); }
void setRenderMode(int mode) { renderer.store(mode, std::memory_order_relaxed); }
void setPixelFormatMode(int mode) { pixelFormat.store(mode, std::memory_order_relaxed); }

}
