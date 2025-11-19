#pragma once

#include <memory>
#include <string>

#include "ConfigManager.hpp"
#include "Limelight.h"
#include "legacy/vita.hpp"
#ifdef BUILD_FFMPEG
#include "ffmpeg/ffmpeg.hpp"
#else
// Forward-declare minimal type to avoid pulling ffmpeg symbols when BUILD_FFMPEG=OFF
typedef struct FFmpegVideoContext FFmpegVideoContext;
#endif

// VideoManager - Gestiona la selección y configuración del sistema de video
class VideoManager
{
  public:
    VideoManager();
    ~VideoManager();

    // Inicialización
    bool initialize();

    // Configuración del modo de renderizado
    void setRenderMode(const std::string& mode);
    std::string getRenderMode() const;

    // Callbacks para Limelight
    DECODER_RENDERER_CALLBACKS getDecoderCallbacks();

    // If the selected renderer needs a render context (FFmpeg wrapper), expose it here
    void* getRenderContext();

    // Control del video
    void startVideo();
    void stopVideo();

    // Instancia singleton
    static VideoManager* instance();

    // Estado
    bool isInitialized() const { return _initialized; }

  private:
    // Contextos de video
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
