#include "VideoManager.hpp"

#include "legacy/vita.hpp"
#ifdef BUILD_FFMPEG
#include "ffmpeg/ffmpeg.hpp"
#endif
#include <borealis/core/logger.hpp>

#include "legacy/modules/vita_globals.hpp"
#include "video/render_mode_cache.hpp"

// Snapshot global consumido por vita.cpp
VideoSettings g_video_settings_snapshot = {};

// Flag para habilitar/deshabilitar debug logs
bool g_debug_log_enabled = false;

// Declaraciones extern para las funciones de vita.cpp
extern "C"
{
    int vitavideo_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags);
    void vitavideo_start();
    void vitavideo_stop();
    void vita_cleanup();
    int vitavideo_submit_decode_unit(PDECODE_UNIT decodeUnit);
}

// Inicializar instancia singleton
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

    // No necesitamos cleanup manual ya que el sistema legacy maneja su propio estado
    // a través de los callbacks de Limelight

    // _instance = nullptr;
}

// Instancia singleton
VideoManager* VideoManager::instance()
{
    static VideoManager* instance = new VideoManager();
    return instance;
}

bool VideoManager::initialize()
{
    if (_initialized)
    {
        return true;
    }

    brls::Logger::info("[VideoManager] Inicializando sistema de video...");

    // Cargar configuración
    _config.load();
    VideoSettings settings = _config.getVideoSettings();
    bool updated           = false;
    if (settings.render_mode != 0 && settings.pixel_format_mode != 0)
    {
        brls::Logger::info("[VideoManager] Reiniciando pixel_format_mode a RGBA porque el modo moderno no lo soporta");
        settings.pixel_format_mode = 0;
        updated                    = true;
    }
    if (updated)
    {
        _config.setVideoSettings(settings);
        _config.save();
    }
    g_video_settings_snapshot = settings; // sincronizar snapshot global
    // Sincronizar flags globales legacy que afectan render inmediato
    video_fullscreen_stretch = settings.fullscreen;
    // low latency removido: ya no se asigna
    _currentModeInt = settings.render_mode;
    _currentMode    = (_currentModeInt == 0) ? "legacy" : "ffmpeg";

    brls::Logger::info("[VideoManager] Modo de renderizado configurado: {}", _currentMode);

    // Si el modo por defecto es ffmpeg, preparar el contexto
#ifdef BUILD_FFMPEG
    if (_currentMode == "ffmpeg")
    {
        if (!_ffmpegContext)
        {
            _ffmpegContext = new FFmpegVideoContext();
            memset(_ffmpegContext, 0, sizeof(FFmpegVideoContext));
            ffmpeg_video_set_render_mode(_ffmpegContext, "ffmpeg");
        }
    }
#endif
    // Inicializar contexto según el modo
    // No necesitamos contextos manuales ya que el sistema legacy maneja su propio estado
    // a través de los callbacks de Limelight

    _initialized = true;
    brls::Logger::info("[VideoManager] Sistema de video inicializado correctamente");
    return true;
}

void VideoManager::setRenderMode(const std::string& mode)
{
    if (_currentMode == mode)
    {
        return;
    }

    brls::Logger::info("[VideoManager] Cambiando modo de renderizado de {} a {}", _currentMode, mode);

    // Detener video actual si está corriendo
    if (_videoRunning)
    {
        stopVideo();
    }

    // Limpiar contexto anterior
#ifdef BUILD_FFMPEG
    if (_currentMode == "ffmpeg" && _ffmpegContext)
    {
        // Cleanup del contexto FFmpeg si existe
        ffmpeg_video_cleanup(_ffmpegContext);
        delete _ffmpegContext;
        _ffmpegContext = nullptr;
    }
#endif

    // Establecer nuevo modo
    _currentMode    = mode;
    _currentModeInt = (mode == "legacy") ? 0 : 1;

    // Si el nuevo modo es ffmpeg, reservar contexto
#ifdef BUILD_FFMPEG
    if (_currentMode == "ffmpeg")
    {
        if (!_ffmpegContext)
        {
            _ffmpegContext = new FFmpegVideoContext();
            memset(_ffmpegContext, 0, sizeof(FFmpegVideoContext));
            ffmpeg_video_set_render_mode(_ffmpegContext, "ffmpeg");
        }
    }
#endif

    // Guardar en configuración
    _config.load();
    VideoSettings settings = _config.getVideoSettings();
    settings.render_mode   = _currentModeInt;
    if (_currentModeInt != 0 && settings.pixel_format_mode != 0)
    {
        brls::Logger::info("[VideoManager] Desactivando formato YUV experimental: sólo disponible en modo legacy");
        settings.pixel_format_mode = 0;
    }
    g_video_settings_snapshot = settings; // actualizar snapshot global
    _config.setVideoSettings(settings);
    _config.save();
    set_render_mode_cached(_currentModeInt);

    // Inicializar nuevo contexto
    // No necesitamos contextos manuales ya que el sistema legacy maneja su propio estado
    // a través de los callbacks de Limelight

    brls::Logger::info("[VideoManager] Modo de renderizado cambiado exitosamente");
}

// Callbacks estáticos que delegan según el modo actual
// Provide access to the internal render context for LiStartConnection
void* VideoManager::getRenderContext()
{
#ifdef BUILD_FFMPEG
    return static_cast<void*>(_ffmpegContext);
#else
    return nullptr;
#endif
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
        // Asignar callbacks directamente a las funciones extern "C" de vita.cpp
        callbacks.setup            = vitavideo_setup;
        callbacks.start            = vitavideo_start;
        callbacks.stop             = vitavideo_stop;
        callbacks.cleanup          = vita_cleanup;
        callbacks.submitDecodeUnit = vitavideo_submit_decode_unit;
        callbacks.capabilities     = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SLICES_PER_FRAME(2);
    }
    else
    {
#ifdef BUILD_FFMPEG
        // Obtener callbacks desde el wrapper FFmpeg
        callbacks = get_ffmpeg_video_callbacks();
#else
        // FFmpeg no está compilado: fallback a legacy callbacks to avoid undefined refs
        callbacks.setup            = vitavideo_setup;
        callbacks.start            = vitavideo_start;
        callbacks.stop             = vitavideo_stop;
        callbacks.cleanup          = vita_cleanup;
        callbacks.submitDecodeUnit = vitavideo_submit_decode_unit;
        callbacks.capabilities     = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SLICES_PER_FRAME(2);
#endif
    }

    brls::Logger::info("[VideoManager] Callbacks configurados para modo {}", _currentMode);
    // Debug: log the function pointer addresses so device memory/stack dumps can be correlated
    brls::Logger::info("[VideoManager] Decoder callback ptrs - setup: {:#x}, start: {:#x}, stop: {:#x}, cleanup: {:#x}, submit: {:#x}",
        reinterpret_cast<uintptr_t>(callbacks.setup),
        reinterpret_cast<uintptr_t>(callbacks.start),
        reinterpret_cast<uintptr_t>(callbacks.stop),
        reinterpret_cast<uintptr_t>(callbacks.cleanup),
        reinterpret_cast<uintptr_t>(callbacks.submitDecodeUnit));
    return callbacks;
}

void VideoManager::startVideo()
{
    if (!_initialized)
    {
        brls::Logger::error("[VideoManager] No se puede iniciar video: sistema no inicializado");
        return;
    }

    if (_videoRunning)
    {
        brls::Logger::warning("[VideoManager] Video ya está corriendo");
        return;
    }

    _config.load();
    VideoSettings settings = _config.getVideoSettings();
    bool needsSave         = false;
    if (settings.render_mode != _currentModeInt)
    {
        _currentModeInt = settings.render_mode;
        _currentMode    = (_currentModeInt == 0) ? "legacy" : "ffmpeg";
        set_render_mode_cached(_currentModeInt);
        brls::Logger::info("[VideoManager] Modo de render actualizado dinámicamente a {}", _currentMode);
    }
    if (_currentModeInt != 0 && settings.pixel_format_mode != 0)
    {
        brls::Logger::info("[VideoManager] Ajustando pixel_format_mode a RGBA para modos no legacy");
        settings.pixel_format_mode = 0;
        needsSave                  = true;
    }
    if (needsSave)
    {
        _config.setVideoSettings(settings);
        _config.save();
    }
    g_video_settings_snapshot = settings;
    video_fullscreen_stretch  = settings.fullscreen;

    brls::Logger::info("[VideoManager] Iniciando video en modo {}", _currentMode);

    if (_currentMode == "legacy")
    {
        // El sistema legacy maneja su propio estado interno a través de callbacks
        brls::Logger::info("[VideoManager] Video legacy iniciado");
    }
    else if (_currentMode == "ffmpeg")
    {
        // TODO: Implementar cuando tengamos FFmpeg
        brls::Logger::info("[VideoManager] Video FFmpeg iniciado (placeholder)");
    }

    _videoRunning = true;
}

void VideoManager::stopVideo()
{
    if (!_videoRunning)
    {
        return;
    }

    brls::Logger::info("[VideoManager] Deteniendo video en modo {}", _currentMode);

    if (_currentMode == "legacy")
    {
        // El sistema legacy maneja su propio cleanup a través de callbacks
        brls::Logger::info("[VideoManager] Video legacy detenido");
    }
    else if (_currentMode == "ffmpeg")
    {
        // TODO: Implementar cuando tengamos FFmpeg
        brls::Logger::info("[VideoManager] Video FFmpeg detenido (placeholder)");
    }

    _videoRunning = false;
}

// Callbacks estáticos para Limelight - REMOVIDOS: ahora usamos directamente las funciones extern "C"
