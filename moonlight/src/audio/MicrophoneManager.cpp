/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#include "MicrophoneManager.hpp"

#include <borealis.hpp>
#include <chrono>
#include <mutex>
#include <thread>

#include "ConfigManager.hpp"
#include "debug.hpp"

MicrophoneManager& MicrophoneManager::getInstance()
{
    static MicrophoneManager instance;
    return instance;
}

MicrophoneManager::MicrophoneManager()
{
    vita_log::info("[MicrophoneManager] Initialized");
}

MicrophoneManager::~MicrophoneManager()
{
    stop();
    vita_log::info("[MicrophoneManager] Destroyed");
}

bool MicrophoneManager::start(const std::string& hostIp, int port, int sampleRate, int channels, int bitrate)
{
    if (running_)
    {
        vita_log::warning("[MicrophoneManager] Already running");
        return true;
    }

    host_ip_     = hostIp;
    port_        = port;
    sample_rate_ = sampleRate;
    channels_    = channels;
    bitrate_     = bitrate;

    vita_log::info("[MicrophoneManager] Starting microphone (%s:%d, %dHz, %dch, %dbps)",
        host_ip_.c_str(), port_, sample_rate_, channels_, bitrate_);

    vita_log::warning("[MicrophoneManager] Delaying microphone start by 3 seconds to avoid port conflict...");

    // Starting immediately can race Moonlight's audio initialization.
    std::this_thread::sleep_for(std::chrono::seconds(3));

    vita_log::info("[MicrophoneManager] Delay complete, attempting to start microphone...");

    if (startInternal())
    {
        vita_log::info("[MicrophoneManager] Started successfully");
        return true;
    }

    vita_log::warning("[MicrophoneManager] Initial start failed, enabling retry every %d seconds",
        RETRY_INTERVAL_SECONDS);

    retry_enabled_ = true;
    retry_thread_  = std::thread(&MicrophoneManager::retryThreadFunc, this);

    return false; // Not running yet, but retry enabled
}

void MicrophoneManager::stop()
{
    vita_log::info("[MicrophoneManager] Stopping...");

    retry_enabled_ = false;
    if (retry_thread_.joinable())
    {
        retry_thread_.join();
        vita_log::debug("[MicrophoneManager] Retry thread stopped");
    }

    if (client_)
    {
        moonmic_destroy(client_);
        client_  = nullptr;
        running_ = false;
        vita_log::info("[MicrophoneManager] Microphone stopped");
    }
}

bool MicrophoneManager::isRunning() const
{
    return running_;
}

bool MicrophoneManager::isRetrying() const
{
    return retry_enabled_;
}

bool MicrophoneManager::isTransmitting()
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    return client_ != nullptr && moonmic_is_connected(client_);
}

int MicrophoneManager::getRTT()
{
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (!client_)
        return -1;
    return moonmic_client_get_rtt(client_);
}

std::string MicrophoneManager::getLastError() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

std::string MicrophoneManager::getHostIp() const
{
    return host_ip_;
}

int MicrophoneManager::getPort() const
{
    return port_;
}

bool MicrophoneManager::startInternal()
{
    if (client_)
    {
        moonmic_destroy(client_);
        client_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_.clear();
    }

    ConfigManager cfg;
    cfg.load();
    VideoSettings settings = cfg.getVideoSettings();

    bool raw_mode = !settings.enable_microphone_compression;

    vita_log::info("[MicrophoneManager] Compression mode: %s (raw_mode=%d)",
        settings.enable_microphone_compression ? "Opus" : "RAW PCM",
        raw_mode);

    auto& bridge           = moonmic::MoonmicBridge::getInstance();
    auto sunInfo           = bridge.buildSunshineValidation(host_ip_);
    int pair_status        = sunInfo.pair_status;
    std::string uniqueid   = sunInfo.uniqueid;
    std::string devicename = sunInfo.devicename;

    vita_log::info("[MicrophoneManager] Sunshine validation result: PairStatus=%d", pair_status);
    vita_log::info("[MicrophoneManager] uniqueid='%s' len=%u", uniqueid.c_str(), (unsigned)uniqueid.length());
    vita_log::info("[MicrophoneManager] devicename='%s' len=%u", devicename.c_str(), (unsigned)devicename.length());

    bridge.loadConfig();
    auto [target_width, target_height] = bridge.getTargetResolution();

    moonmic_config_t config = {
        .host_ip     = host_ip_.c_str(),
        .port        = static_cast<uint16_t>(port_),
        .sample_rate = static_cast<uint32_t>(sample_rate_),
        .channels    = static_cast<uint8_t>(channels_),
        .bitrate     = static_cast<uint32_t>(bitrate_),
        .raw_mode    = raw_mode,
        .auto_start  = true,
        .gain        = settings.microphone_gain,

        .uniqueid            = uniqueid.c_str(),
        .devicename          = devicename.c_str(),
        .sunshine_https_port = 47984,
        .pair_status         = pair_status,

        .target_display_width  = target_width,
        .target_display_height = target_height
    };

    client_ = moonmic_create(&config);

    if (!client_)
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = "Failed to create microphone client";
        vita_log::error("[MicrophoneManager] %s", last_error_.c_str());
        return false;
    }

    moonmic_set_error_callback(client_, &MicrophoneManager::errorCallback, this);

    running_       = true;
    retry_enabled_ = false;

    vita_log::info("[MicrophoneManager] Client created successfully");
    return true;
}

void MicrophoneManager::retryThreadFunc()
{
    vita_log::info("[MicrophoneManager] Retry thread started");

    while (retry_enabled_)
    {
        for (int i = 0; i < RETRY_INTERVAL_SECONDS && retry_enabled_; ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!retry_enabled_)
            break;

        vita_log::info("[MicrophoneManager] Retrying connection to %s:%d", host_ip_.c_str(), port_);

        if (startInternal())
        {
            vita_log::info("[MicrophoneManager] Retry successful - microphone now running");
            break;
        }
        else
        {
            vita_log::warning("[MicrophoneManager] Retry failed, will try again in %d seconds",
                RETRY_INTERVAL_SECONDS);
        }
    }

    vita_log::info("[MicrophoneManager] Retry thread stopped");
}

void MicrophoneManager::errorCallback(const char* error, void* userData)
{
    auto* mgr = static_cast<MicrophoneManager*>(userData);
    if (!mgr)
        return;

    {
        std::lock_guard<std::mutex> lock(mgr->error_mutex_);
        mgr->last_error_ = error ? error : "Unknown error";
    }

    vita_log::error("[MicrophoneManager] moonmic error: %s",
        error ? error : "Unknown error");
}

void MicrophoneManager::setGain(float gain)
{
    ConfigManager config;
    config.updateVideoSettings([gain](VideoSettings& settings)
        { settings.microphone_gain = gain; });

    if (running_ && client_)
    {
        moonmic_set_gain(client_, gain);
        vita_log::info("[MicrophoneManager] Updated gain to %.1fx during transmission", gain);
    }
}

bool MicrophoneManager::isConnected() const
{
    if (!running_ || !client_)
    {
        return false;
    }
    return moonmic_is_connected(client_);
}
