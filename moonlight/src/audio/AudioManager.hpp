#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// AudioManager: interface to handle incoming audio packets (Moonlight stream)
// Nearby goals:
//  - Audio JIT buffer for synchronization with video
//  - Jitter, losses and underruns statistics
//  - Possible resampling if host changes frequency
//  - Simple mixer for future UI sounds

struct AudioPacket
{
    uint32_t frameIndex; // index or block number
    uint64_t ptsMs; // estimated timestamp (ms)
    std::vector<uint8_t> data; // encrypted data received (for now passthrough)
};

struct AudioStats
{
    uint32_t packetsReceived     = 0;
    uint32_t packetsLost         = 0;
    uint32_t unrecoverableBlocks = 0;
    uint32_t latePackets         = 0;
    uint32_t queuedFrames        = 0;
    uint32_t underruns           = 0;
    uint64_t lastPtsMs           = 0;
};

class AudioManager
{
  public:
    static AudioManager& instance();

    // Feed a received audio packet (unpacked from network)
    void pushPacket(const AudioPacket& pkt);

    // Called periodically (e.g. from main loop) to drain and play
    void update(uint64_t nowMs);

    // Obtener stats (snapshot)
    AudioStats getStats() const;

    // Reset (new session)
    void reset();

    // Initial settings (format, sample rate, etc.)
    void configure(int sampleRate, int channels, const std::string& codecName);

  private:
    AudioManager()                               = default;
    AudioManager(const AudioManager&)            = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    mutable AudioStats stats;
    int sampleRate = 0;
    int channels   = 0;
    std::string codec;
    // TODO: implement efficient circular queue (simple vector for now)
    std::vector<AudioPacket> queue;
};
