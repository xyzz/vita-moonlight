#include "VideoManager.hpp"

#include <borealis/core/logger.hpp>

#include "debug.hpp"
#include "ffmpeg/ffmpeg.hpp"
#include "legacy/modules/vita_globals.hpp"
#include "legacy/vita.hpp"
#include "video/render_mode_cache.hpp"

// Global snapshot consumed by vita.cpp
VideoSettings g_video_settings_snapshot = {};
bool g_gpu_yuv_experimental_enabled     = false;

// Flag to enable/disable debug logs
bool g_debug_log_enabled = false;

// extern declarations for vita.cpp functions
extern "C"
{
    int vitavideo_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags);
    void vitavideo_start();
    void vitavideo_stop();
    void vita_cleanup();
    int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit);
}

// Initialize singleton instance
// VideoManager* VideoManager::_instance = nullptr;

VideoManager::VideoManager()
    : _ffmpegContext(nullptr)
    , _currentMode("legacy")
    , _currentModeInt(0)
    , _initialized(false)
    , _videoRunning(false)
{
    // _instance = this;
}

VideoManager::~VideoManager()
{
    if (_videoRunning)
    {
        stopVideo();
    }

    // We do not need manual cleanup since the legacy system manages its own state
    // via Limelight callbacks

    // _instance = nullptr;
}

// singleton instance
VideoManager* VideoManager::instance()
{
    static VideoManager* instance = new VideoManager();
    return instance;
}

bool VideoManager::initialize()
{
    if (_initialized)
    {
        // If already initialized but video is not running, refresh the settings from disk.
        // This ensures the next session gets the correct callbacks for the newly selected mode.
        if (!_videoRunning)
        {
            _config.load();
            VideoSettings settings         = _config.getVideoSettings();
            g_video_settings_snapshot      = settings;
            const char* env                = getenv("MOONLIGHT_FFMPEG_GPU_YUV");
            g_gpu_yuv_experimental_enabled = (env && env[0] == '1') || (settings.pixel_format_mode == 1);
            video_fullscreen_stretch       = settings.fullscreen;
            _currentModeInt                = settings.render_mode;
            _currentMode                   = (_currentModeInt == 0) ? "legacy" : "ffmpeg";
            vita_log::info("[VideoManager] Re-inicializando modo de renderizado: %s", _currentMode.c_str());
            if (_currentMode == "ffmpeg")
            {
                if (!_ffmpegContext)
                {
                    _ffmpegContext = new FFmpegVideoContext();
                    memset(_ffmpegContext, 0, sizeof(FFmpegVideoContext));
                    ffmpeg_video_set_render_mode(_ffmpegContext, "ffmpeg");
                }
            }
            else
            {
                if (_ffmpegContext)
                {
                    ffmpeg_video_cleanup(_ffmpegContext);
                    delete _ffmpegContext;
                    _ffmpegContext = nullptr;
                }
            }
        }
        return true;
    }

    vita_log::info("[VideoManager] Inicializando sistema de video...");

    // Load configuration
    _config.load();
    VideoSettings settings         = _config.getVideoSettings();
    g_video_settings_snapshot      = settings;
    const char* env                = getenv("MOONLIGHT_FFMPEG_GPU_YUV");
    g_gpu_yuv_experimental_enabled = (env && env[0] == '1') || (settings.pixel_format_mode == 1);
    // Synchronize legacy global flags that affect immediate render
    video_fullscreen_stretch = settings.fullscreen;
    // low latency removed: no longer assigned
    _currentModeInt = settings.render_mode;
    _currentMode    = (_currentModeInt == 0) ? "legacy" : "ffmpeg";

    vita_log::info("[VideoManager] Modo de renderizado configurado: %s", _currentMode.c_str());

    // If the default mode is ffmpeg, prepare the context
    if (_currentMode == "ffmpeg")
    {
        if (!_ffmpegContext)
        {
            _ffmpegContext = new FFmpegVideoContext();
            memset(_ffmpegContext, 0, sizeof(FFmpegVideoContext));
            ffmpeg_video_set_render_mode(_ffmpegContext, "ffmpeg");
        }
    }
    // Initialize context based on mode
    // We do not need manual contexts since the legacy system manages its own state
    // via Limelight callbacks

    _initialized = true;
    vita_log::info("[VideoManager] Sistema de video inicializado correctamente");
    return true;
}

void VideoManager::setRenderMode(const std::string& mode)
{
    if (_currentMode == mode)
    {
        return;
    }

    vita_log::info("[VideoManager] Cambiando modo de renderizado de %s a %s", _currentMode.c_str(), mode.c_str());

    // Stop current video if it is running
    if (_videoRunning)
    {
        stopVideo();
    }

    // Clear previous context
    if (_currentMode == "ffmpeg" && _ffmpegContext)
    {
        // Cleanup of FFmpeg context if it exists
        ffmpeg_video_cleanup(_ffmpegContext);
        delete _ffmpegContext;
        _ffmpegContext = nullptr;
    }

    // Set new mode
    _currentMode    = mode;
    _currentModeInt = (mode == "legacy") ? 0 : 1;

    // If new mode is ffmpeg, reserve context
    if (_currentMode == "ffmpeg")
    {
        if (!_ffmpegContext)
        {
            _ffmpegContext = new FFmpegVideoContext();
            memset(_ffmpegContext, 0, sizeof(FFmpegVideoContext));
            ffmpeg_video_set_render_mode(_ffmpegContext, "ffmpeg");
        }
    }

    // Save to settings
    _config.load();
    VideoSettings settings         = _config.getVideoSettings();
    settings.render_mode           = _currentModeInt;
    g_video_settings_snapshot      = settings; // update global snapshot
    const char* env                = getenv("MOONLIGHT_FFMPEG_GPU_YUV");
    g_gpu_yuv_experimental_enabled = (env && env[0] == '1') || (settings.pixel_format_mode == 1);
    _config.setVideoSettings(settings);
    _config.save();
    set_render_mode_cached(_currentModeInt);

    // Initialize new context
    // We do not need manual contexts since the legacy system manages its own state
    // via Limelight callbacks

    vita_log::info("[VideoManager] Modo de renderizado cambiado exitosamente");
}

// Static callbacks that delegate based on current mode
// Provide access to the internal render context for LiStartConnection
void* VideoManager::getRenderContext()
{
    return static_cast<void*>(_ffmpegContext);
}

std::string VideoManager::getRenderMode() const
{
    return _currentMode;
}

DECODER_RENDERER_CALLBACKS VideoManager::getDecoderCallbacks()
{
    DECODER_RENDERER_CALLBACKS callbacks = { 0 };

    if (_currentMode == "legacy")
    {
        // Assign callbacks directly to the extern "C" functions of vita.cpp
        callbacks.setup            = vitavideo_setup;
        callbacks.start            = vitavideo_start;
        callbacks.stop             = vitavideo_stop;
        callbacks.cleanup          = vita_cleanup;
        callbacks.submitDecodeUnit = vitavideo_submit_decode_unit;
        callbacks.capabilities     = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SLICES_PER_FRAME(2);
    }
    else
    {
        // Get callbacks from the FFmpeg wrapper
        callbacks = get_ffmpeg_video_callbacks();
    }

    vita_log::info("[VideoManager] Callbacks configurados para modo %s", _currentMode.c_str());
    // Debug: log the function pointer addresses so device memory/stack dumps can be correlated
    vita_log::info("[VideoManager] Decoder callback ptrs - setup: %p, start: %p, stop: %p, cleanup: %p, submit: %p",
        (void*)callbacks.setup,
        (void*)callbacks.start,
        (void*)callbacks.stop,
        (void*)callbacks.cleanup,
        (void*)callbacks.submitDecodeUnit);
    return callbacks;
}

void VideoManager::startVideo()
{
    if (!_initialized)
    {
        vita_log::error("[VideoManager] No se puede iniciar video: sistema no inicializado");
        return;
    }

    if (_videoRunning)
    {
        vita_log::warning("[VideoManager] Video ya está corriendo");
        return;
    }

    _config.load();
    VideoSettings settings = _config.getVideoSettings();
    if (settings.render_mode != _currentModeInt)
    {
        _currentModeInt = settings.render_mode;
        _currentMode    = (_currentModeInt == 0) ? "legacy" : "ffmpeg";
        set_render_mode_cached(_currentModeInt);
        vita_log::info("[VideoManager] Modo de render actualizado dinámicamente a %s", _currentMode.c_str());
    }
    g_video_settings_snapshot      = settings;
    const char* env                = getenv("MOONLIGHT_FFMPEG_GPU_YUV");
    g_gpu_yuv_experimental_enabled = (env && env[0] == '1') || (settings.pixel_format_mode == 1);
    video_fullscreen_stretch       = settings.fullscreen;

    vita_log::info("[VideoManager] Iniciando video en modo %s", _currentMode.c_str());

    if (_currentMode == "legacy")
    {
        // The legacy system manages its own internal state through callbacks
        vita_log::info("[VideoManager] Video legacy iniciado");
    }
    else if (_currentMode == "ffmpeg")
    {
        vita_log::info("[VideoManager] Video FFmpeg iniciado");
    }

    _videoRunning = true;
}

void VideoManager::stopVideo()
{
    if (!_videoRunning)
    {
        return;
    }

    vita_log::info("[VideoManager] Deteniendo video en modo %s", _currentMode.c_str());

    if (_currentMode == "legacy")
    {
        // The legacy system handles its own cleanup through callbacks
        vita_log::info("[VideoManager] Video legacy detenido");
    }
    else if (_currentMode == "ffmpeg")
    {
        if (_ffmpegContext)
        {
            ffmpeg_video_stop(_ffmpegContext);
        }
        vita_log::info("[VideoManager] Video FFmpeg detenido");
    }

    _videoRunning = false;
}

// Static callbacks for Limelight - REMOVED: we now directly use extern "C" functions
