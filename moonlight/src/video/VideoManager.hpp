#pragma once

#include <mutex>
#include <string>

#include "ConfigManager.hpp"
#include "Limelight.h"
#include "ffmpeg/ffmpeg.hpp"
#include "legacy/vita.hpp"

class VideoManager
{
  public:
    VideoManager();
    ~VideoManager();

    bool initialize();
    std::string getRenderMode() const;
    DECODER_RENDERER_CALLBACKS getDecoderCallbacks();
    void* getRenderContext();
    void startVideo();
    void stopVideo();
    static VideoManager* instance();
    bool isInitialized() const;

  private:
    FFmpegVideoContext* _ffmpegContext;
    std::string _currentMode;
    bool _initialized;
    bool _videoRunning;
    mutable std::recursive_mutex _mutex;
    ConfigManager _config;

    void refreshSettings();
    void prepareFfmpegContext();
    void releaseFfmpegContext();
};
