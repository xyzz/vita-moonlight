#include "session/vita_session.hpp"

#include <borealis/core/application.hpp>
#include <cstring>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "Limelight.h"
#include "controller/ControllerInput.hpp"
#include "controller/audio.hpp"
#include "debug.hpp"
#include "session/connection_telemetry.hpp"
#include "session/stream_config.hpp"
#include "utils/vita_platform.hpp"
#include "video/VideoManager.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/modules/vita_globals.hpp"
#include "video/legacy/vita.hpp"

std::mutex VitaSession::s_active_mutex;
std::weak_ptr<VitaSession> VitaSession::s_active;

VitaSession::VitaSession(const std::string& address, int appId)
    : m_address(address)
    , m_app_id(appId)
{
    vita_log::info("[VitaSession] creada address=%s appId=%d", address.c_str(), appId);
}

VitaSession::~VitaSession()
{
    stop(false);
    std::lock_guard<std::mutex> lock(s_active_mutex);
    auto activeSession = s_active.lock();
    if (!activeSession || activeSession.get() == this)
        s_active.reset();
    vita_log::info("[VitaSession] destruida");
}

std::shared_ptr<VitaSession> VitaSession::active()
{
    std::lock_guard<std::mutex> lock(s_active_mutex);
    return s_active.lock();
}

void VitaSession::notifyGamepadType()
{
    auto session = active();
    if (!session || !session->m_is_active.load())
        return;

    ConfigManager config;
    config.load();
    VideoSettings settings = config.getVideoSettings();
    GamepadType type       = settings.gamepad_type;

    uint8_t liType        = (type == GAMEPAD_TYPE_PS4) ? 0x02 : 0x01;
    uint16_t capabilities = LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE;
    if (type == GAMEPAD_TYPE_PS4)
    {
        capabilities |= LI_CCAP_TOUCHPAD;
    }
    uint32_t supportedButtonFlags = 0xFFFFFFFF;

    if (LiSendControllerArrivalEvent(0, 0x01, liType, supportedButtonFlags, capabilities) != 0)
    {
        vita_log::error("[VitaSession] Fallo notificar tipo de gamepad al servidor");
    }
    else
    {
        vita_log::info("[VitaSession] Tipo de gamepad notificado al servidor (LI_CTYPE=%d)", liType);
    }
}

bool VitaSession::start()
{
    std::lock_guard<std::mutex> lifecycleLock(m_lifecycle_mutex);
    if (m_is_active.load())
        return true;
    m_start_attempted  = true;
    m_cleanup_complete = false;
    {
        std::lock_guard<std::mutex> activeLock(s_active_mutex);
        s_active = shared_from_this();
    }
    connection::setTelemetryAvailable(false);
    STREAM_CONFIGURATION prev {};
    bool havePrev = GameStreamClient::instance().lastStreamConfig(m_address, prev);
    if (havePrev)
    {
        m_config = prev;
        vita_log::info("[VitaSession] Reutilizando configuración previa");
    }
    else
    {
        ConfigManager config;
        config.load();
        m_config = stream::buildConfiguration(config.getStreamConfig());
    }
    if ((m_config.supportedVideoFormats & ~VIDEO_FORMAT_H264) != 0)
    {
        m_config.supportedVideoFormats = VIDEO_FORMAT_H264;
    }

    LiInitializeConnectionCallbacks(&m_conn_callbacks);
    m_conn_callbacks.stageStarting          = connection_stage_starting;
    m_conn_callbacks.stageComplete          = connection_stage_complete;
    m_conn_callbacks.stageFailed            = connection_stage_failed;
    m_conn_callbacks.connectionStarted      = connection_started;
    m_conn_callbacks.connectionTerminated   = connection_terminated;
    m_conn_callbacks.logMessage             = connection_log_message;
    m_conn_callbacks.connectionStatusUpdate = connection_status_update;

    if (!VideoManager::instance()->initialize())
    {
        vita_log::error("[VitaSession] Fallo al inicializar VideoManager");
        return false;
    }
    vita_log::info("[VitaSession] Decodificador inicializado: %s", VideoManager::instance()->getRenderMode().c_str());
    LiInitializeVideoCallbacks(&m_video_callbacks);
    m_video_callbacks = VideoManager::instance()->getDecoderCallbacks();

    LiInitializeAudioCallbacks(&m_audio_callbacks);
    m_audio_callbacks.init                = audio_renderer_init;
    m_audio_callbacks.start               = audio_renderer_start;
    m_audio_callbacks.stop                = audio_renderer_stop;
    m_audio_callbacks.cleanup             = audio_renderer_cleanup;
    m_audio_callbacks.decodeAndPlaySample = audio_renderer_decode_and_play_sample;
    m_audio_callbacks.capabilities |= CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION;

    if (internalStart())
        return true;

    std::lock_guard<std::mutex> activeLock(s_active_mutex);
    auto activeSession = s_active.lock();
    if (activeSession.get() == this)
        s_active.reset();
    return false;
}

bool VitaSession::internalStart()
{
    SERVER_INFORMATION serverInfo {};
    if (!GameStreamClient::instance().serverInfo(m_address, serverInfo))
    {
        vita_log::error("[VitaSession] No hay información para %s", m_address.c_str());
        return false;
    }
    void* renderContext = VideoManager::instance()->getRenderContext();
    int res             = LiStartConnection(&serverInfo, &m_config, &m_conn_callbacks, &m_video_callbacks, &m_audio_callbacks, renderContext, 0, nullptr, 0);
    if (res != 0)
    {
        vita_log::error("[VitaSession] LiStartConnection fallo=%d", res);
        LiStopConnection();
        return false;
    }
    vita_log::info("[VitaSession] LiStartConnection ok (%dx%d@%d fps bitrate=%dK formats=0x%X)", m_config.width, m_config.height, m_config.fps, m_config.bitrate, m_config.supportedVideoFormats);
    VideoManager::instance()->startVideo();
    vita_log::info("[VitaSession] Video iniciado con decodificador: %s", VideoManager::instance()->getRenderMode().c_str());
    m_is_active              = true;
    m_is_terminated          = false;
    m_allow_resume_reconnect = true;
    m_reconnect_attempts     = 0;
    connection::setTelemetryAvailable(true);
    return true;
}

void VitaSession::stop(bool terminateApp)
{
    std::lock_guard<std::mutex> lifecycleLock(m_lifecycle_mutex);
    m_allow_resume_reconnect = false;
    if (!m_start_attempted || m_cleanup_complete)
        return;

    connection::suspendTelemetry();

    if (g_controllerInput)
        g_controllerInput->setStreamingActive(false);

    VideoManager::instance()->stopVideo();
    if (terminateApp)
        GameStreamClient::instance().quitApp(m_address);
    LiStopConnection();
    m_is_active        = false;
    m_is_terminated    = true;
    m_start_attempted  = false;
    m_cleanup_complete = true;
}

bool VitaSession::attemptReconnect()
{
    if (m_reconnect_attempts.load() >= m_reconnect_limit)
        return false;
    const int attempt = m_reconnect_attempts.fetch_add(1) + 1;
    vita_log::info("[VitaSession] Reconnect intento %d", attempt);

    return restartConnection(false);
}

bool VitaSession::reconnectAfterResume()
{
    if (!m_allow_resume_reconnect)
    {
        vita_log::info("[VitaSession] Reconnect tras resume omitido: sesión no reanudable");
        return true;
    }

    vita_log::info("[VitaSession] Reanudación de Vita detectada; renovando sesión y stream");
    return restartConnection(true);
}

bool VitaSession::restartConnection(bool renegotiateSession)
{
    std::lock_guard<std::mutex> lifecycleLock(m_lifecycle_mutex);
    connection::suspendTelemetry();
    VideoManager::instance()->stopVideo();
    LiStopConnection();
    m_is_active     = false;
    m_is_terminated = false;

    if (renegotiateSession && !GameStreamClient::instance().resumeApp(m_address, m_config, m_app_id))
    {
        m_is_terminated = true;
        return false;
    }

    if (!internalStart())
    {
        m_is_terminated = true;
        return false;
    }

    if (g_controllerInput)
    {
        g_controllerInput->setStreamingActive(true);
        g_controllerInput->dropInput();
    }
    notifyGamepadType();
    return true;
}

void VitaSession::onFrameDecoded()
{
    auto session = active();
    if (!session)
        return;
    std::lock_guard<std::mutex> lock(session->m_stats_mutex);
    auto& st     = session->m_stats;
    uint64_t now = vita::monotonicMilliseconds();
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
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    VitaOverlaySnapshot snap {};
    VitaVideoStats vs {};
    vitavideo_get_stats(&vs);
    snap.fps_presented    = vs.current_fps ? vs.current_fps : m_stats.fps;
    snap.fps_target       = vs.target_fps;
    snap.session_ms       = m_stats.firstFrameTimestampMs ? (vita::monotonicMilliseconds() - m_stats.firstFrameTimestampMs) : 0;
    snap.frames_decoded   = vs.frames_decoded;
    snap.frames_presented = vs.frames_presented;
    return snap;
}

void VitaSession::connection_stage_starting(int stage) { vita_log::info("[VitaSession] Stage starting %d", stage); }
void VitaSession::connection_stage_complete(int stage) { vita_log::info("[VitaSession] Stage complete %d", stage); }
void VitaSession::connection_stage_failed(int stage, int error_code) { vita_log::error("[VitaSession] Stage failed %d ec=%d", stage, error_code); }
void VitaSession::connection_started()
{
    auto session = active();
    if (session)
        session->m_is_active = true;
}
void VitaSession::connection_terminated(int error_code)
{
    vita_log::info("[VitaSession] Connection terminated ec=%d", error_code);
    connection::setTelemetryAvailable(false);
    auto session = active();
    if (!session)
        return;
    session->m_is_active     = false;
    session->m_is_terminated = true;
}
void VitaSession::connection_log_message(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vita_log::vlog("LIMELIGHT", format, args);
    va_end(args);
}
void VitaSession::connection_status_update(int status)
{
    vita_log::info("[VitaSession] Connection status %d", status);
}

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
