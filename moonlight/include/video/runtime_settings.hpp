#pragma once

struct VideoSettings;

namespace video_runtime
{

void apply(const VideoSettings& settings);

bool motionControlsEnabled();
float motionScaleX();
float motionScaleY();
bool showFps();
bool centerRegionOnly();
bool fullscreen();
bool gpuYuvEnabled();
int renderMode();
int pixelFormatMode();

void setShowFps(bool enabled);
void setMotionControlsEnabled(bool enabled);
void setMotionScaleX(float scale);
void setMotionScaleY(float scale);
void setRenderMode(int mode);
void setPixelFormatMode(int mode);

}
