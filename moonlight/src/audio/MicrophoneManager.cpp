/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#include "MicrophoneManager.hpp"

#include <borealis.hpp>

#include "ConfigManager.hpp"

MicrophoneManager& MicrophoneManager::getInstance()
{
    static MicrophoneManager instance;
    return instance;
}

MicrophoneManager::MicrophoneManager()
{
    brls::Logger::info("[MicrophoneManager] Initialized");
}

MicrophoneManager::~MicrophoneManager()
{
    stop();
    brls::Logger::info("[MicrophoneManager] Destroyed");
}

bool MicrophoneManager::start(const std::string& hostIp, int port, int sampleRate, int channels, int bitrate)
{
    if (running_)
    {
        brls::Logger::warning("[MicrophoneManager] Already running");
        return true;
    }

    host_ip_     = hostIp;
    port_        = port;
    sample_rate_ = sampleRate;
    channels_    = channels;
    bitrate_     = bitrate;

    brls::Logger::info("[MicrophoneManager] Starting microphone ({}:{}, {}Hz, {}ch, {}bps)",
        host_ip_, port_, sample_rate_, channels_, bitrate_);

    brls::Logger::warning("[MicrophoneManager] Delaying microphone start by 3 seconds to avoid port conflict...");

    std::this_thread::sleep_for(std::chrono::seconds(3));

    brls::Logger::info("[MicrophoneManager] Delay complete, attempting to start microphone...");

    if (startInternal())
    {
        brls::Logger::info("[MicrophoneManager] Started successfully");
        return true;
    }

    brls::Logger::warning("[MicrophoneManager] Initial start failed, enabling retry every {} seconds",
        RETRY_INTERVAL_SECONDS);

    retry_enabled_ = true;
    retry_thread_  = std::thread(&MicrophoneManager::retryThreadFunc, this);

    return false; // Not running yet, but retry enabled
}

void MicrophoneManager::stop()
{
    brls::Logger::info("[MicrophoneManager] Stopping...");

    retry_enabled_ = false;
    if (retry_thread_.joinable())
    {
        retry_thread_.join();
        brls::Logger::debug("[MicrophoneManager] Retry thread stopped");
    }

    if (client_)
    {
        moonmic_destroy(client_);
        client_  = nullptr;
        running_ = false;
        brls::Logger::info("[MicrophoneManager] Microphone stopped");
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

    brls::Logger::info("[MicrophoneManager] Compression mode: {} (raw_mode={})",
        settings.enable_microphone_compression ? "Opus" : "RAW PCM",
        raw_mode);

    // Configure libmoonmic
    moonmic_config_t config = {
        .host_ip     = host_ip_.c_str(),
        .port        = static_cast<uint16_t>(port_),
        .sample_rate = static_cast<uint32_t>(sample_rate_),
        .channels    = static_cast<uint8_t>(channels_),
        .bitrate     = static_cast<uint32_t>(bitrate_),
        .raw_mode    = raw_mode, // Set based on UI toggle
        .auto_start  = true, // Start capturing immediately
        .gain        = settings.microphone_gain // Gain multiplier from config
    };

    client_ = moonmic_create(&config);

    if (!client_)
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = "Failed to create microphone client";
        brls::Logger::error("[MicrophoneManager] {}", last_error_);
        return false;
    }

    moonmic_set_error_callback(client_, &MicrophoneManager::errorCallback, this);

    running_       = true;
    retry_enabled_ = false; // Stop retry thread if it was running

    brls::Logger::info("[MicrophoneManager] Client created successfully");
    return true;
}

void MicrophoneManager::retryThreadFunc()
{
    brls::Logger::info("[MicrophoneManager] Retry thread started");

    while (retry_enabled_)
    {
        for (int i = 0; i < RETRY_INTERVAL_SECONDS && retry_enabled_; ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!retry_enabled_)
            break;

        brls::Logger::info("[MicrophoneManager] Retrying connection to {}:{}", host_ip_, port_);

        if (startInternal())
        {
            brls::Logger::info("[MicrophoneManager] Retry successful - microphone now running");
            break; // Exit retry loop
        }
        else
        {
            brls::Logger::warning("[MicrophoneManager] Retry failed, will try again in {} seconds",
                RETRY_INTERVAL_SECONDS);
        }
    }

    brls::Logger::info("[MicrophoneManager] Retry thread stopped");
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

    brls::Logger::error("[MicrophoneManager] libmoonmic error: {}",
        error ? error : "Unknown error");
}

void MicrophoneManager::setGain(float gain)
{
    ConfigManager config;
    config.load();
    VideoSettings settings   = config.getVideoSettings();
    settings.microphone_gain = gain;
    config.setVideoSettings(settings);
    config.save();

    if (running_ && client_)
    {
        moonmic_set_gain(client_, gain);
        brls::Logger::info("[MicrophoneManager] Updated gain to {:.1f}x during transmission", gain);
    }
}
