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

class MicrophoneManager
{
  public:
    static MicrophoneManager& getInstance();

    bool start(const std::string& hostIp,
        int port       = MOONMIC_DEFAULT_PORT,
        int sampleRate = MOONMIC_DEFAULT_SAMPLE_RATE,
        int channels   = MOONMIC_DEFAULT_CHANNELS,
        int bitrate    = MOONMIC_DEFAULT_BITRATE);

    void stop();
    int getRTT();
    bool isTransmitting();
    bool isRunning() const;
    bool isRetrying() const;
    bool isConnected() const;
    std::string getLastError() const;
    std::string getHostIp() const;
    int getPort() const;
    void setGain(float gain);

  private:
    MicrophoneManager();
    ~MicrophoneManager();

    MicrophoneManager(const MicrophoneManager&)            = delete;
    MicrophoneManager& operator=(const MicrophoneManager&) = delete;

    void retryThreadFunc();
    bool startInternal();
    static void errorCallback(const char* error, void* userData);

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
