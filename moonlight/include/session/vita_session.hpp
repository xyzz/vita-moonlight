#pragma once
#include <string>

#include "GameStreamClient.hpp"
#include "Limelight.h"
#include "borealis.hpp"
#include "client.h"

// Estructura ligera de estadísticas (placeholder, ampliar luego)
struct VitaSessionStats
{
    uint64_t videoFrames           = 0; // frames decodificados (onFrameDecoded)
    uint64_t videoBytes            = 0; // futuro: acumular tamaño AU
    uint64_t lastFrameNumber       = 0; // no usado aún (para drops network en Fase3)
    uint64_t firstFrameTimestampMs = 0;
    // Ventana FPS
    uint32_t windowFrames  = 0;
    uint64_t windowStartMs = 0;
    uint32_t fps           = 0; // calculado cada ~1000ms
};

// Mini snapshot para overlay (agregado sin exponer VitaVideoStats directamente)
struct VitaOverlaySnapshot
{
    uint32_t fps_presented    = 0;
    uint32_t fps_target       = 0;
    uint64_t session_ms       = 0;
    uint32_t frames_decoded   = 0;
    uint32_t frames_presented = 0;
};

class VitaSession
{
  public:
    VitaSession(const std::string& address, int appId, bool isSunshine);
    ~VitaSession();

    // Inicia la sesión (devuelve bool inmediato; logs profundos en callbacks)
    bool start();
    void stop(bool terminateApp);

    bool isActive() const { return m_is_active; }
    bool isTerminated() const { return m_is_terminated; }

    const VitaSessionStats& stats() const { return m_stats; }

    // Reconexión manual
    bool attemptReconnect();

    // Acceso a la sesión activa
    static VitaSession* active();
    // Destruye (stop + free) la sesión activa si existe
    static void destroyActive(bool terminateApp);

    // Notificación desde el decoder cuando un frame queda listo (post swap)
    static void onFrameDecoded();

    // Dibuja frame actual
    void draw(float viewportW, float viewportH);

    // Obtiene snapshot para overlay (usa vitavideo_get_stats internamente)
    VitaOverlaySnapshot overlaySnapshot() const;

    // Notificar al servidor del tipo de gamepad guardado en config
    static void notifyGamepadType();

  private:
    // Callbacks estáticos estilo Moonlight-Switch
    static void connection_stage_starting(int stage);
    static void connection_stage_complete(int stage);
    static void connection_stage_failed(int stage, int error_code);
    static void connection_started();
    static void connection_terminated(int error_code);
    static void connection_log_message(const char* format, ...);
    static void connection_status_update(int status);

    static int video_decoder_setup(int, int, int, int, void*, int);
    static void video_decoder_start();
    static void video_decoder_stop();
    static void video_decoder_cleanup();
    static int video_decoder_submit_decode_unit(PDECODE_UNIT);

    static int audio_renderer_init(int, const POPUS_MULTISTREAM_CONFIGURATION, void*, int);
    static void audio_renderer_start();
    static void audio_renderer_stop();
    static void audio_renderer_cleanup();
    static void audio_renderer_decode_and_play_sample(char*, int);

    bool internalStart();

  private:
    std::string m_address;
    int m_app_id;
    bool m_is_sunshine;

    STREAM_CONFIGURATION m_config {};
    CONNECTION_LISTENER_CALLBACKS m_conn_callbacks {};
    DECODER_RENDERER_CALLBACKS m_video_callbacks {};
    AUDIO_RENDERER_CALLBACKS m_audio_callbacks {};

    bool m_is_active     = false;
    bool m_is_terminated = false;
    bool m_poor          = false;

    int m_reconnect_attempts    = 0;
    const int m_reconnect_limit = 1; // simple por ahora

    VitaSessionStats m_stats {};
    uint64_t m_startMonotonicMs = 0;
    static uint64_t monotonicMs();

    static VitaSession* s_active;
};
