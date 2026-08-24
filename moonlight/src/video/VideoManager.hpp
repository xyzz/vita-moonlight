#pragma once

#include <memory>
#include <string>

#include "ConfigManager.hpp"
#include "Limelight.h"
#include "ffmpeg/ffmpeg.hpp"
#include "legacy/vita.hpp"

extern bool g_gpu_yuv_experimental_enabled;

// VideoManager - Manage video system selection and configuration
class VideoManager
{
  public:
    VideoManager();
    ~VideoManager();

    // Initialization
    bool initialize();

    // Rendering mode settings
    void setRenderMode(const std::string& mode);
    std::string getRenderMode() const;

    // Limelight callbacks.
    DECODER_RENDERER_CALLBACKS getDecoderCallbacks();

    // If the selected renderer needs a render context (FFmpeg wrapper), expose it here
    void* getRenderContext();

    // Video control
    void startVideo();
    void stopVideo();

    // singleton instance
    static VideoManager* instance();

    // Estado
    bool isInitialized() const { return _initialized; }

  private:
    // Video contexts
    FFmpegVideoContext* _ffmpegContext;

    // Estado
    std::string _currentMode;
    int _currentModeInt;
    bool _initialized;
    bool _videoRunning;

    // ConfigManager
    ConfigManager _config;

    // Callbacks internos
    static int videoSetupCallback(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags);
    static void videoStartCallback(void);
    static void videoStopCallback(void);
    static void videoCleanupCallback(void);
    static int videoSubmitDecodeUnitCallback(PDECODE_UNIT decodeUnit);
};
