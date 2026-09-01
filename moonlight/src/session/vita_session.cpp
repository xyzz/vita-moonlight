// vita_session.cpp - Stub Phase1 (minimal administration and frame notification only)
#include "session/vita_session.hpp"

#include <borealis/core/application.hpp>
#include <chrono>
#include <cstring>
#include <thread>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "Limelight.h"
#include "controller/ControllerInput.hpp"
#include "controller/audio.hpp"
#include "debug.hpp"
#include "video/VideoManager.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/modules/vita_globals.hpp"
#include "video/legacy/vita.hpp" // vitavideo_get_stats, decoder callbacks
#ifdef BOREALIS_USE_GXM
#include <borealis/extern/nanovg/nanovg_gxm_utils.h>
#include <psp2/gxm.h>
#endif

namespace
{
void wait_for_borealis_gxm_idle()
{
#ifdef BOREALIS_USE_GXM
    NVGXMwindow* win = gxmGetWindow();
    if (win && win->context)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#endif
}
}

VitaSession* VitaSession::s_active = nullptr;

VitaSession::VitaSession(const std::string& address, int appId, bool isSunshine)
    : m_address(address)
    , m_app_id(appId)
    , m_is_sunshine(isSunshine)
{
    s_active = this;
    vita_log::info("[VitaSession] creada address=%s appId=%d sunshine=%d", address.c_str(), appId, isSunshine);
}

VitaSession::~VitaSession()
{
    if (s_active == this)
        s_active = nullptr;
    vita_log::info("[VitaSession] destruida");
}

VitaSession* VitaSession::active() { return s_active; }

void VitaSession::notifyGamepadType()
{
    if (!s_active || !s_active->m_is_active)
        return;

    // Load gamepad type from settings
    ConfigManager config;
    config.load();
    VideoSettings settings = config.getVideoSettings();
    GamepadType type       = settings.gamepad_type;

    // Send to server: LI_CTYPE_XBOX = 0x01, LI_CTYPE_PS = 0x02
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
        vita_log::info("[VitaSession] deferred delete watcher started");

        // Wait until the session reports terminated and the pacer thread is gone.
        // Timeout after ~5 seconds to avoid leaking forever in pathological cases.
        const int maxAttempts = 50; // 50 * 100ms = 5s
        int attempt = 0;
        while (attempt++ < maxAttempts) {
            if (!s_active) break;
            if (s_active->m_is_terminated && !active_pacer_thread) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Ensure any pending rendering on Borealis GXM context is finished before finalizing.
        vita_log::info("[VitaSession] waiting for GXM rendering to finish before delete");
        wait_for_borealis_gxm_idle();

        // Small back-off to further reduce race-window.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (s_active) {
            vita_log::info("[VitaSession] performing deferred delete now");
            delete s_active;
        } })
        .detach();
}

bool VitaSession::start()
{
    if (m_is_active)
        return true;
    g_session_stopping = false;
    // Try to reuse previous configuration (to maintain keys) if it exists
    STREAM_CONFIGURATION prev {};
    bool havePrev = GameStreamClient::instance().lastStreamConfig(m_address, prev);
    if (havePrev)
    {
        m_config = prev;
        vita_log::info("[VitaSession] Reutilizando configuración previa");
    }
    else
    {
        LiInitializeStreamConfiguration(&m_config);
        m_config.width              = 960;
        m_config.height             = 544;
        m_config.fps                = 60;
        m_config.bitrate            = 8000;
        m_config.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
        m_config.streamingRemotely  = STREAM_CFG_AUTO;
        m_config.packetSize         = 1024;
#ifdef VIDEO_FORMAT_H264
        m_config.supportedVideoFormats = VIDEO_FORMAT_H264; // H.264 only on Vita
#endif
        m_config.encryptionFlags = m_is_sunshine ? ENCFLG_ALL : ENCFLG_NONE;
    }
    // Clear any non-H.264 bits
    if ((m_config.supportedVideoFormats & ~VIDEO_FORMAT_H264) != 0)
    {
        m_config.supportedVideoFormats = VIDEO_FORMAT_H264;
    }

    // Initialize connection callbacks
    LiInitializeConnectionCallbacks(&m_conn_callbacks);
    m_conn_callbacks.stageStarting          = connection_stage_starting;
    m_conn_callbacks.stageComplete          = connection_stage_complete;
    m_conn_callbacks.stageFailed            = connection_stage_failed;
    m_conn_callbacks.connectionStarted      = connection_started;
    m_conn_callbacks.connectionTerminated   = connection_terminated;
    m_conn_callbacks.logMessage             = connection_log_message;
    m_conn_callbacks.connectionStatusUpdate = connection_status_update;

    // Video callbacks: use VideoManager to choose between legacy and FFmpeg
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

    return internalStart();
}

bool VitaSession::internalStart()
{
    SERVER_DATA& srv = GameStreamClient::instance().serverData(m_address);
    // Pass render context in renderContext
    void* renderContext = VideoManager::instance()->getRenderContext();
    int res             = LiStartConnection(&srv.serverInfo, &m_config, &m_conn_callbacks, &m_video_callbacks, &m_audio_callbacks, renderContext, 0, nullptr, 0);
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
    return true;
}

void VitaSession::stop(bool terminateApp)
{
    m_allow_resume_reconnect = false;
    if (!m_is_active && !m_is_terminated)
        return;

    // Set stopping flag to prevent concurrent ENet RTT telemetry queries
    g_session_stopping = true;

    // Allow in-flight LiGetEstimatedRttInfo queries to finish executing safely
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Disable PS button capture so Vita OS handles it normally again
    if (g_controllerInput)
        g_controllerInput->setStreamingActive(false);

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
    vita_log::info("[VitaSession] Reconnect intento %d", m_reconnect_attempts);

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

    g_session_stopping = true;
    VideoManager::instance()->stopVideo();
    LiStopConnection();
    m_is_active     = false;
    m_is_terminated = false;

    if (renegotiateSession && !GameStreamClient::instance().resumeApp(m_address, m_config, m_app_id))
    {
        g_session_stopping = false;
        m_is_terminated    = true;
        return false;
    }

    g_session_stopping = false;

    if (!internalStart())
    {
        m_is_terminated = true;
        return false;
    }

    if (g_controllerInput)
    {
        g_controllerInput->setStreamingActive(true);
        g_controllerInput->lockPSButton();
        g_controllerInput->dropInput();
    }
    notifyGamepadType();
    return true;
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
void VitaSession::connection_stage_starting(int stage) { vita_log::info("[VitaSession] Stage starting %d", stage); }
void VitaSession::connection_stage_complete(int stage) { vita_log::info("[VitaSession] Stage complete %d", stage); }
void VitaSession::connection_stage_failed(int stage, int error_code) { vita_log::error("[VitaSession] Stage failed %d ec=%d", stage, error_code); }
void VitaSession::connection_started()
{
    if (s_active)
        s_active->m_is_active = true;
}
void VitaSession::connection_terminated(int error_code)
{
    vita_log::info("[VitaSession] Connection terminated ec=%d", error_code);
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

// ==== Video callbacks (hooks delegated to decode current vita) ====
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
