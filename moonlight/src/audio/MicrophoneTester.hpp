/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/

#pragma once

#include <atomic>
#include <thread>

/**
 * @brief Local microphone testing with audio loopback
 *
 * Captures audio from the PS Vita microphone and plays it back through
 * the speakers/headphones in real-time. Supports both RAW and Opus modes
 * to test if audio quality issues are from capture or compression.
 *
 * Usage:
 *   auto& tester = MicrophoneTester::getInstance();
 *   tester.setOpusMode(true);   // Test with Opus compression
 *   tester.start();  // Begin loopback
 *   // ... user can hear their voice ...
 *   tester.stop();   // End loopback
 */
class MicrophoneTester
{
  public:
    /**
     * @brief Get singleton instance
     */
    static MicrophoneTester& getInstance();

    /**
     * @brief Start microphone loopback test
     * @return true if started successfully, false on error
     */
    bool start();

    /**
     * @brief Stop microphone loopback test
     */
    void stop();

    /**
     * @brief Enable/disable Opus compression test
     * @param enabled true = test with Opus encode/decode, false = RAW PCM
     */
    void setOpusMode(bool enabled);

    /**
     * @brief Check if loopback is currently active
     * @return true if running
     */
    bool isRunning() const;

  private:
    MicrophoneTester();
    ~MicrophoneTester();

    MicrophoneTester(const MicrophoneTester&)            = delete;
    MicrophoneTester& operator=(const MicrophoneTester&) = delete;

    /**
     * @brief Loopback thread function
     * Continuously reads from microphone and writes to audio output
     */
    void loopbackThreadFunc();

    std::atomic<bool> running_ { false };
    std::atomic<bool> use_opus_ { false }; // Test with Opus compression?
    std::thread loopback_thread_;
};
