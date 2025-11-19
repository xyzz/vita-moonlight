// vita_session.cpp - Stub Fase1 (solo administración mínima y notificación de frames)
#include "session/vita_session.hpp"

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <chrono>
#include <cstring>
#include <thread>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "Limelight.h"
#include "controller/ControllerInput.hpp"
#include "controller/audio.hpp"
#include "video/VideoManager.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/modules/vita_globals.hpp"
#include "video/legacy/vita.hpp" // vitavideo_get_stats, decoder callbacks

VitaSession* VitaSession::s_active = nullptr;

VitaSession::VitaSession(const std::string& address, int appId, bool isSunshine)
    : m_address(address)
    , m_app_id(appId)
    , m_is_sunshine(isSunshine)
{
    s_active = this;
    brls::Logger::info("[VitaSession] creada address={} appId={} sunshine={}", address, appId, isSunshine);
}

VitaSession::~VitaSession()
{
    if (s_active == this)
        s_active = nullptr;
    brls::Logger::info("[VitaSession] destruida");
}

VitaSession* VitaSession::active() { return s_active; }

void VitaSession::notifyGamepadType()
{
    if (!s_active || !s_active->m_is_active)
        return;

    // Cargar el tipo de gamepad desde configuración
    ConfigManager config;
    config.load();
    VideoSettings settings = config.getVideoSettings();
    GamepadType type       = settings.gamepad_type;

    // Enviar al servidor: LI_CTYPE_XBOX = 0x01, LI_CTYPE_PS = 0x02
    uint8_t liType                = (type == GAMEPAD_TYPE_PS4) ? 0x02 : 0x01;
    uint16_t capabilities         = 0x01 | 0x02; // ANALOG_TRIGGERS | RUMBLE
    uint32_t supportedButtonFlags = 0xFFFFFFFF;

    if (LiSendControllerArrivalEvent(0, 0x01, liType, supportedButtonFlags, capabilities) != 0)
    {
        brls::Logger::error("[VitaSession] Fallo notificar tipo de gamepad al servidor");
    }
    else
    {
        brls::Logger::info("[VitaSession] Tipo de gamepad notificado al servidor (LI_CTYPE={})", liType);
    }
}

void VitaSession::destroyActive(bool terminateApp)
{
    if (!s_active)
        return;

    // Stop the session immediately but defer the actual delete until we're
    // confident video/decoder/GXM resources are fully quiescent. Deleting
    // the object while the pacer/decoder/renderer are still active can
    // cause SCE_GXM driver crashes on the Vita.
    s_active->stop(terminateApp);

    // Spawn a detached thread to watch termination state and active_pacer_thread
    // then perform a safe delete. This mirrors the safe teardown ordering
    // used in the legacy implementation and avoids deleting from UI thread
    // while GPU calls may still be in-flight.
    std::thread([]()
        {
        brls::Logger::info("[VitaSession] deferred delete watcher started");

        // Wait until the session reports terminated and the pacer thread is gone.
        // Timeout after ~5 seconds to avoid leaking forever in pathological cases.
        const int maxAttempts = 50; // 50 * 100ms = 5s
        int attempt = 0;
        while (attempt++ < maxAttempts) {
            if (!s_active) break;
            if (s_active->m_is_terminated && !active_pacer_thread) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Ensure any pending vita2d rendering is finished before finalizing.
        if (vita2d_inited) {
            brls::Logger::info("[VitaSession] waiting for vita2d rendering to finish before delete");
            // Best-effort: wait for rendering to drain.
            vita2d_wait_rendering_done();
        }

        // Small back-off to further reduce race-window.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (s_active) {
            brls::Logger::info("[VitaSession] performing deferred delete now");
            delete s_active;
        } })
        .detach();
}

bool VitaSession::start()
{
    if (m_is_active)
        return true;
    // Intentar reutilizar configuración previa (para mantener keys) si existe
    STREAM_CONFIGURATION prev {};
    bool havePrev = GameStreamClient::instance().lastStreamConfig(m_address, prev);
    if (havePrev)
    {
        m_config = prev;
        brls::Logger::info("[VitaSession] Reutilizando configuración previa");
    }
    else
    {
        LiInitializeStreamConfiguration(&m_config);
        m_config.width              = 960;
        m_config.height             = 544;
        m_config.fps                = 60; // objetivo 60
        m_config.bitrate            = 8000; // Kbps (placeholder; luego leer settings)
        m_config.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
        m_config.streamingRemotely  = STREAM_CFG_AUTO;
        m_config.packetSize         = 1024;
#ifdef VIDEO_FORMAT_H264
        m_config.supportedVideoFormats = VIDEO_FORMAT_H264; // Solo H.264 en Vita
#endif
        m_config.encryptionFlags = m_is_sunshine ? ENCFLG_ALL : ENCFLG_NONE;
    }
    // Limpiar cualquier bit no H.264
    if ((m_config.supportedVideoFormats & ~VIDEO_FORMAT_H264) != 0)
    {
        m_config.supportedVideoFormats = VIDEO_FORMAT_H264;
    }

    // Inicializar callbacks conexión
    LiInitializeConnectionCallbacks(&m_conn_callbacks);
    m_conn_callbacks.stageStarting          = connection_stage_starting;
    m_conn_callbacks.stageComplete          = connection_stage_complete;
    m_conn_callbacks.stageFailed            = connection_stage_failed;
    m_conn_callbacks.connectionStarted      = connection_started;
    m_conn_callbacks.connectionTerminated   = connection_terminated;
    m_conn_callbacks.logMessage             = connection_log_message;
    m_conn_callbacks.connectionStatusUpdate = connection_status_update;

    // Video callbacks: usar VideoManager para elegir entre legacy y FFmpeg
    if (!VideoManager::instance()->initialize())
    {
        brls::Logger::error("[VitaSession] Fallo al inicializar VideoManager");
        return false;
    }
    brls::Logger::info("[VitaSession] Decodificador inicializado: {}", VideoManager::instance()->getRenderMode());
    LiInitializeVideoCallbacks(&m_video_callbacks);
    m_video_callbacks = VideoManager::instance()->getDecoderCallbacks();

    // Audio callbacks stub por ahora
    LiInitializeAudioCallbacks(&m_audio_callbacks);
    m_audio_callbacks.init                = audio_renderer_init;
    m_audio_callbacks.start               = audio_renderer_start;
    m_audio_callbacks.stop                = audio_renderer_stop;
    m_audio_callbacks.cleanup             = audio_renderer_cleanup;
    m_audio_callbacks.decodeAndPlaySample = audio_renderer_decode_and_play_sample;

    return internalStart();
}

bool VitaSession::internalStart()
{
    SERVER_DATA& srv = GameStreamClient::instance().serverData(m_address);
    // Pasar el contexto de renderizado en renderContext
    void* renderContext = VideoManager::instance()->getRenderContext();
    int res             = LiStartConnection(&srv.serverInfo, &m_config, &m_conn_callbacks, &m_video_callbacks, &m_audio_callbacks, renderContext, 0, nullptr, 0);
    if (res != 0)
    {
        brls::Logger::error("[VitaSession] LiStartConnection fallo={} ", res);
        LiStopConnection();
        return false;
    }
    brls::Logger::info("[VitaSession] LiStartConnection ok ({}x{}@{} fps bitrate={}K formats=0x{:X})", m_config.width, m_config.height, m_config.fps, m_config.bitrate, m_config.supportedVideoFormats);
    VideoManager::instance()->startVideo();
    brls::Logger::info("[VitaSession] Video iniciado con decodificador: {}", VideoManager::instance()->getRenderMode());
    m_is_active     = true;
    m_is_terminated = false;
    return true;
}

void VitaSession::stop(bool terminateApp)
{
    if (!m_is_active && !m_is_terminated)
        return;
    VideoManager::instance()->stopVideo();
    if (terminateApp)
        GameStreamClient::instance().quitApp(m_address);
    LiStopConnection();
    m_is_active     = false;
    m_is_terminated = true;
}

bool VitaSession::attemptReconnect()
{
    if (m_reconnect_attempts >= m_reconnect_limit)
        return false;
    m_reconnect_attempts++;
    brls::Logger::info("[VitaSession] Reconnect intento {}", m_reconnect_attempts);
    // Simple: detener y reiniciar
    LiStopConnection();
    return internalStart();
}

void VitaSession::onFrameDecoded()
{
    if (!s_active)
        return;
    auto& st                   = s_active->m_stats;
    static uint32_t logCounter = 0;
    st.videoFrames++;
    uint64_t now = monotonicMs();
    // if (logCounter < 120 || (logCounter % 60) == 0) {
    //     VITA_DEBUG_LOG("[VitaSession] onFrameDecoded count=%u framesDecoded=%u presented=%u", logCounter, g_stats.frames_decoded, g_stats.frames_presented);
    // }
    logCounter++;
    if (!st.firstFrameTimestampMs)
    {
        st.firstFrameTimestampMs = now;
        st.windowStartMs         = now;
        st.windowFrames          = 1;
    }
    else
    {
        st.windowFrames++;
        if (now - st.windowStartMs >= 1000)
        {
            st.fps           = st.windowFrames * 1000 / (uint32_t)(now - st.windowStartMs);
            st.windowFrames  = 0;
            st.windowStartMs = now;
        }
    }
}

void VitaSession::draw(float viewportW, float viewportH) { VitaVideoRenderer::instance().draw(viewportW, viewportH); }

VitaOverlaySnapshot VitaSession::overlaySnapshot() const
{
    VitaOverlaySnapshot snap {};
    VitaVideoStats vs {};
    vitavideo_get_stats(&vs);
    snap.fps_presented    = vs.current_fps ? vs.current_fps : m_stats.fps;
    snap.fps_target       = vs.target_fps;
    snap.session_ms       = m_stats.firstFrameTimestampMs ? (monotonicMs() - m_stats.firstFrameTimestampMs) : 0;
    snap.frames_decoded   = vs.frames_decoded;
    snap.frames_presented = vs.frames_presented;
    return snap;
}

uint64_t VitaSession::monotonicMs()
{
    extern uint64_t vita_monotonic_ms();
    return vita_monotonic_ms();
}

// ==== Connection callbacks ====
void VitaSession::connection_stage_starting(int stage) { brls::Logger::info("[VitaSession] Stage starting {}", stage); }
void VitaSession::connection_stage_complete(int stage) { brls::Logger::info("[VitaSession] Stage complete {}", stage); }
void VitaSession::connection_stage_failed(int stage, int error_code) { brls::Logger::error("[VitaSession] Stage failed {} ec={}", stage, error_code); }
void VitaSession::connection_started()
{
    if (s_active)
        s_active->m_is_active = true;
}
void VitaSession::connection_terminated(int error_code)
{
    brls::Logger::info("[VitaSession] Connection terminated ec={}", error_code);
    if (!s_active)
        return;
    s_active->m_is_active     = false;
    s_active->m_is_terminated = true;
}
void VitaSession::connection_log_message(const char* format, ...) { (void)format; }
void VitaSession::connection_status_update(int status)
{
    if (s_active)
        s_active->m_poor = (status == CONN_STATUS_POOR);
}

// ==== Video callbacks (hooks delegados a decoder vita actual) ====
int VitaSession::video_decoder_setup(int a, int b, int c, int d, void* e, int f)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    return 0;
}
void VitaSession::video_decoder_start() { }
void VitaSession::video_decoder_stop() { }
void VitaSession::video_decoder_cleanup() { }
int VitaSession::video_decoder_submit_decode_unit(PDECODE_UNIT) { return 0; }

// ==== Audio callbacks (stubs) ====
int VitaSession::audio_renderer_init(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* audioContext, int arFlags)
{
    return controller::audio_init(audioConfiguration, opusConfig, audioContext, arFlags);
}

void VitaSession::audio_renderer_start()
{
    controller::audio_start();
}

void VitaSession::audio_renderer_stop()
{
    controller::audio_stop();
}

void VitaSession::audio_renderer_cleanup()
{
    controller::audio_cleanup();
}

void VitaSession::audio_renderer_decode_and_play_sample(char* data, int length)
{
    controller::audio_decode_and_play_sample(data, length);
}
