#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "GameStreamClient.hpp"
#include "Limelight.h"
#include "borealis.hpp"
#include "client.h"

struct VitaSessionStats
{
    uint64_t firstFrameTimestampMs = 0;
    uint32_t windowFrames          = 0;
    uint64_t windowStartMs         = 0;
    uint32_t fps                   = 0;
};

struct VitaOverlaySnapshot
{
    uint32_t fps_presented    = 0;
    uint32_t fps_target       = 0;
    uint64_t session_ms       = 0;
    uint32_t frames_decoded   = 0;
    uint32_t frames_presented = 0;
};

class VitaSession : public std::enable_shared_from_this<VitaSession>
{
  public:
    VitaSession(const std::string& address, int appId);
    ~VitaSession();

    bool start();
    void stop(bool terminateApp);

    bool isActive() const { return m_is_active.load(); }
    bool isTerminated() const { return m_is_terminated.load(); }
    const std::string& address() const { return m_address; }

    bool attemptReconnect();
    bool reconnectAfterResume();

    static std::shared_ptr<VitaSession> active();
    static void onFrameDecoded();
    void draw(float viewportW, float viewportH);
    VitaOverlaySnapshot overlaySnapshot() const;
    static void notifyGamepadType();

  private:
    static void connection_stage_starting(int stage);
    static void connection_stage_complete(int stage);
    static void connection_stage_failed(int stage, int error_code);
    static void connection_started();
    static void connection_terminated(int error_code);
    static void connection_log_message(const char* format, ...);
    static void connection_status_update(int status);

    static int audio_renderer_init(int, const POPUS_MULTISTREAM_CONFIGURATION, void*, int);
    static void audio_renderer_start();
    static void audio_renderer_stop();
    static void audio_renderer_cleanup();
    static void audio_renderer_decode_and_play_sample(char*, int);

    bool internalStart();
    bool restartConnection(bool renegotiateSession);

  private:
    std::string m_address;
    int m_app_id;
    STREAM_CONFIGURATION m_config {};
    CONNECTION_LISTENER_CALLBACKS m_conn_callbacks {};
    DECODER_RENDERER_CALLBACKS m_video_callbacks {};
    AUDIO_RENDERER_CALLBACKS m_audio_callbacks {};

    std::atomic<bool> m_is_active { false };
    std::atomic<bool> m_is_terminated { false };
    std::atomic<bool> m_allow_resume_reconnect { false };

    std::atomic<int> m_reconnect_attempts { 0 };
    const int m_reconnect_limit = 1;

    mutable std::mutex m_lifecycle_mutex;
    bool m_start_attempted  = false;
    bool m_cleanup_complete = false;
    mutable std::mutex m_stats_mutex;
    VitaSessionStats m_stats {};
    static std::mutex s_active_mutex;
    static std::weak_ptr<VitaSession> s_active;
};
