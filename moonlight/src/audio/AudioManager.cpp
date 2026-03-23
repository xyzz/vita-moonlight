#include "AudioManager.hpp"

#include <algorithm>

AudioManager& AudioManager::instance()
{
    static AudioManager inst;
    return inst;
}

void AudioManager::configure(int sr, int ch, const std::string& codecName)
{
    sampleRate = sr;
    channels   = ch;
    codec      = codecName;
    reset();
}

void AudioManager::reset()
{
    queue.clear();
    stats = AudioStats {};
}

void AudioManager::pushPacket(const AudioPacket& pkt)
{
    stats.packetsReceived++;
    if (pkt.ptsMs < stats.lastPtsMs)
        stats.latePackets++;
    stats.lastPtsMs = pkt.ptsMs;
    queue.push_back(pkt);
    stats.queuedFrames = (uint32_t)queue.size();
}

void AudioManager::update(uint64_t nowMs)
{
    // Placeholder: simple drain (no actual playback yet)
    // Policy: keep <= 60 packets in queue, discard older ones if exceed
    const size_t MAX_QUEUE = 60;
    if (queue.size() > MAX_QUEUE)
    {
        size_t drop = queue.size() - MAX_QUEUE;
        stats.packetsLost += (uint32_t)drop; // count as logical loss due to local overflow
        queue.erase(queue.begin(), queue.begin() + drop);
    }
    stats.queuedFrames = (uint32_t)queue.size();
    (void)nowMs;
}

AudioStats AudioManager::getStats() const { return stats; }
