/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "../moonmic/MoonmicBridge.hpp"

/**
 * @brief Singleton manager for microphone audio transmission using libmoonmic
 *
 * Handles microphone lifecycle with automatic retry logic if host is not detected.
 * Retries connection every 10 seconds when enabled but not connected.
 */
class MicrophoneManager
{
  public:
    /**
     * @brief Get singleton instance
     */
    static MicrophoneManager& getInstance();

    /**
     * @brief Start microphone transmission
     * @param hostIp Host IP address to send audio to
     * @param port UDP port (default: 48100)
     * @param sampleRate Sample rate in Hz (default: 48000)
     * @param channels Number of channels - 1=mono, 2=stereo (default: 1)
     * @param bitrate Opus bitrate in bps (default: 64000)
     * @return true if started successfully, false if will retry
     */
    bool start(const std::string& hostIp,
        int port       = MOONMIC_DEFAULT_PORT,
        int sampleRate = MOONMIC_DEFAULT_SAMPLE_RATE,
        int channels   = MOONMIC_DEFAULT_CHANNELS,
        int bitrate    = MOONMIC_DEFAULT_BITRATE);

    /**
     * @brief Stop microphone transmission and retry thread
     */
    void stop();

    /**
     * @brief Get microphone network round-trip time
     * @return RTT in ms, or -1 if unavailable
     */
    int getRTT();

    /**
     * @brief Check if actually transmitting (connected to host)
     */
    bool isTransmitting();

    /**
     * @brief Check if microphone is currently running
     * @return true if transmitting audio
     */
    bool isRunning() const;

    /**
     * @brief Check if retry is enabled (attempting to connect)
     * @return true if retry thread is active
     */
    bool isRetrying() const;

    /**
     * @brief Check heartbeat connection status
     * @return true if connected and receiving heartbeats
     */
    bool isConnected() const;

    /**
     * @brief Get last error message
     * @return Error message or empty string
     */
    std::string getLastError() const;

    /**
     * @brief Get current host IP
     * @return Host IP address
     */
    std::string getHostIp() const;

    /**
     * @brief Get current port
     * @return UDP port
     */
    int getPort() const;

    /**
     * @brief Update gain multiplier dynamically (applies immediately if running)
     * @param gain New gain multiplier (1.0 = no change, higher = louder)
     */
    void setGain(float gain);

  private:
    MicrophoneManager();
    ~MicrophoneManager();

    MicrophoneManager(const MicrophoneManager&)            = delete;
    MicrophoneManager& operator=(const MicrophoneManager&) = delete;

    /**
     * @brief Retry thread function - attempts connection every 10 seconds
     */
    void retryThreadFunc();

    /**
     * @brief Internal start function (called by retry thread)
     * @return true if started successfully
     */
    bool startInternal();

    /**
     * @brief Error callback from libmoonmic
     */
    static void errorCallback(const char* error, void* userData);

    // libmoonmic client instance
    moonmic_client_t* client_ = nullptr;

    std::string host_ip_;
    int port_        = MOONMIC_DEFAULT_PORT;
    int sample_rate_ = MOONMIC_DEFAULT_SAMPLE_RATE;
    int channels_    = MOONMIC_DEFAULT_CHANNELS;
    int bitrate_     = MOONMIC_DEFAULT_BITRATE;

    std::atomic<bool> running_ { false };
    std::atomic<bool> retry_enabled_ { false };
    std::thread retry_thread_;

    std::string last_error_;
    mutable std::mutex error_mutex_;
    mutable std::mutex client_mutex_;

    static constexpr int RETRY_INTERVAL_SECONDS = 10;
};
