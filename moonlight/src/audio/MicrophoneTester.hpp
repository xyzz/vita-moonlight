/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#pragma once

#include <atomic>
#include <thread>

class MicrophoneTester
{
  public:
    static MicrophoneTester& getInstance();
    bool start();
    void stop();
    void setOpusMode(bool enabled);
    void setGain(float gain);
    bool isRunning() const;

  private:
    MicrophoneTester();
    ~MicrophoneTester();

    MicrophoneTester(const MicrophoneTester&)            = delete;
    MicrophoneTester& operator=(const MicrophoneTester&) = delete;

    void loopbackThreadFunc();

    std::atomic<bool> running_ { false };
    std::atomic<bool> use_opus_ { false };
    std::atomic<float> gain_ { 1.0f };
    std::thread loopback_thread_;
};
