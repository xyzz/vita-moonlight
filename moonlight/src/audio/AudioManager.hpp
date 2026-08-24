#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct AudioPacket
{
    uint32_t frameIndex;
    uint64_t ptsMs;
    std::vector<uint8_t> data;
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

    void pushPacket(const AudioPacket& pkt);

    void update(uint64_t nowMs);

    AudioStats getStats() const;

    void reset();

    void configure(int sampleRate, int channels, const std::string& codecName);

  private:
    AudioManager()                               = default;
    AudioManager(const AudioManager&)            = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    mutable AudioStats stats;
    int sampleRate = 0;
    int channels   = 0;
    std::string codec;
    std::vector<AudioPacket> queue;
};
