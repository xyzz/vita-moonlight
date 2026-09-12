#include "VideoManager.hpp"

#include "debug.hpp"
#include "ffmpeg/ffmpeg.hpp"
#include "legacy/modules/vita_globals.hpp"
#include "legacy/vita.hpp"
#include "video/runtime_settings.hpp"

extern "C"
{
    int vitavideo_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags);
    void vitavideo_start();
    void vitavideo_stop();
    void vita_cleanup();
    int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit);
}

VideoManager::VideoManager()
    : _ffmpegContext(nullptr)
    , _currentMode("legacy")
    , _initialized(false)
    , _videoRunning(false)
{
}

VideoManager::~VideoManager()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_videoRunning)
    {
        stopVideo();
    }
    releaseFfmpegContext();
}

VideoManager* VideoManager::instance()
{
    static VideoManager* instance = new VideoManager();
    return instance;
}

bool VideoManager::initialize()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_initialized)
    {
        if (!_videoRunning)
        {
            refreshSettings();
            vita_log::info("[VideoManager] Re-inicializando modo de renderizado: %s", _currentMode.c_str());
            if (_currentMode == "ffmpeg")
            {
                prepareFfmpegContext();
            }
            else
            {
                releaseFfmpegContext();
            }
        }
        return true;
    }

    vita_log::info("[VideoManager] Inicializando sistema de video...");

    refreshSettings();

    vita_log::info("[VideoManager] Modo de renderizado configurado: %s", _currentMode.c_str());

    if (_currentMode == "ffmpeg")
    {
        prepareFfmpegContext();
    }
    _initialized = true;
    vita_log::info("[VideoManager] Sistema de video inicializado correctamente");
    return true;
}

void* VideoManager::getRenderContext()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return static_cast<void*>(_ffmpegContext);
}

std::string VideoManager::getRenderMode() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _currentMode;
}

DECODER_RENDERER_CALLBACKS VideoManager::getDecoderCallbacks()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    DECODER_RENDERER_CALLBACKS callbacks = { 0 };

    if (_currentMode == "legacy")
    {
        callbacks.setup            = vitavideo_setup;
        callbacks.start            = vitavideo_start;
        callbacks.stop             = vitavideo_stop;
        callbacks.cleanup          = vita_cleanup;
        callbacks.submitDecodeUnit = vitavideo_submit_decode_unit;
        callbacks.capabilities     = CAPABILITY_SLICES_PER_FRAME(2);
    }
    else
    {
        callbacks = get_ffmpeg_video_callbacks();
    }

    vita_log::info("[VideoManager] Callbacks configurados para modo %s", _currentMode.c_str());
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
    std::lock_guard<std::recursive_mutex> lock(_mutex);
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

    refreshSettings();
    vita_log::info("[VideoManager] Iniciando video en modo %s", _currentMode.c_str());

    if (_currentMode == "legacy")
    {
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
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_videoRunning)
    {
        return;
    }

    vita_log::info("[VideoManager] Deteniendo video en modo %s", _currentMode.c_str());

    if (_currentMode == "legacy")
    {
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

bool VideoManager::isInitialized() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _initialized;
}

void VideoManager::refreshSettings()
{
    _config.load();
    VideoSettings settings   = _config.getVideoSettings();
    _currentMode             = settings.render_mode == 0 ? "legacy" : "ffmpeg";
    video_fullscreen_stretch = settings.fullscreen;
    video_runtime::apply(settings);
}

void VideoManager::prepareFfmpegContext()
{
    if (_ffmpegContext)
        return;
    _ffmpegContext = new FFmpegVideoContext();
    memset(_ffmpegContext, 0, sizeof(FFmpegVideoContext));
    ffmpeg_video_set_render_mode(_ffmpegContext, "ffmpeg");
}

void VideoManager::releaseFfmpegContext()
{
    if (!_ffmpegContext)
        return;
    ffmpeg_video_cleanup(_ffmpegContext);
    delete _ffmpegContext;
    _ffmpegContext = nullptr;
}
