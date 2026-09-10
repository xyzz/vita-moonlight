#include "session/stream_config.hpp"

#include <algorithm>

namespace stream
{
namespace
{

    constexpr int DEFAULT_FPS           = 60;
    constexpr int DEFAULT_BITRATE       = 6000;
    constexpr int DEFAULT_PACKET_SIZE   = 1024;
    constexpr int MIN_AUTO_BITRATE      = 5000;
    constexpr int MAX_AUTO_BITRATE      = 20000;
    constexpr long long BPP_NUMERATOR   = 18;
    constexpr long long BPP_DENOMINATOR = 100;

    constexpr std::array<Resolution, 8> STREAM_RESOLUTIONS = { {
        { 848, 480 },
        { 960, 540 },
        { 960, 544 },
        { 1024, 576 },
        { 1152, 648 },
        { 1280, 540 },
        { 1280, 720 },
        { 1366, 768 },
    } };

    constexpr std::array<Resolution, 9> HOST_RESOLUTIONS = { {
        { 0, 0 },
        { 960, 544 },
        { 1024, 576 },
        { 1152, 648 },
        { 1280, 540 },
        { 1280, 720 },
        { 1366, 768 },
        { 1600, 900 },
        { 1920, 1080 },
    } };

    constexpr std::array<int, 5> FRAME_RATES = { 24, 30, 40, 50, 60 };
    constexpr std::array<int, 9> BITRATES    = { -1, 2000, 5000, 8000, 10000, 15000, 20000, 30000, 50000 };

    template <typename T, std::size_t N>
    std::size_t valueIndex(const std::array<T, N>& values, const T& value, std::size_t fallback)
    {
        const auto found = std::find(values.begin(), values.end(), value);
        return found == values.end() ? std::min(fallback, values.size() - 1)
                                     : static_cast<std::size_t>(found - values.begin());
    }

    template <std::size_t N>
    std::size_t resolutionIndex(const std::array<Resolution, N>& resolutions,
        int width, int height, std::size_t fallback)
    {
        const auto found = std::find_if(resolutions.begin(), resolutions.end(),
            [width, height](const Resolution& resolution)
            { return resolution.width == width && resolution.height == height; });
        return found == resolutions.end() ? std::min(fallback, resolutions.size() - 1)
                                          : static_cast<std::size_t>(found - resolutions.begin());
    }

}

const std::array<Resolution, 8>& supportedStreamResolutions()
{
    return STREAM_RESOLUTIONS;
}

const std::array<Resolution, 9>& supportedHostResolutions()
{
    return HOST_RESOLUTIONS;
}

const std::array<int, 5>& supportedFrameRates()
{
    return FRAME_RATES;
}

const std::array<int, 9>& supportedBitrates()
{
    return BITRATES;
}

bool isSupportedStreamResolution(int width, int height)
{
    return std::find_if(STREAM_RESOLUTIONS.begin(), STREAM_RESOLUTIONS.end(),
               [width, height](const Resolution& resolution)
               { return resolution.width == width && resolution.height == height; })
        != STREAM_RESOLUTIONS.end();
}

std::size_t streamResolutionIndex(int width, int height, std::size_t fallback)
{
    return resolutionIndex(STREAM_RESOLUTIONS, width, height, fallback);
}

std::size_t hostResolutionIndex(int width, int height, std::size_t fallback)
{
    return resolutionIndex(HOST_RESOLUTIONS, width, height, fallback);
}

std::size_t frameRateIndex(int fps, std::size_t fallback)
{
    return valueIndex(FRAME_RATES, fps, fallback);
}

std::size_t bitrateIndex(int bitrate, std::size_t fallback)
{
    return valueIndex(BITRATES, bitrate, fallback);
}

int calculateAutoBitrate(int width, int height, int fps)
{
    const long long pixelsPerSecond = static_cast<long long>(width) * height * fps;
    const int bitrate               = static_cast<int>(pixelsPerSecond * BPP_NUMERATOR / (BPP_DENOMINATOR * 1000));
    return std::max(MIN_AUTO_BITRATE, std::min(MAX_AUTO_BITRATE, bitrate));
}

STREAM_CONFIGURATION buildConfiguration(const StreamConfiguration& settings)
{
    STREAM_CONFIGURATION config;
    LiInitializeStreamConfiguration(&config);

    config.width                 = settings.streamWidth;
    config.height                = settings.streamHeight;
    config.fps                   = settings.fps > 0 ? settings.fps : DEFAULT_FPS;
    config.bitrate               = settings.bitrate == -1
                      ? calculateAutoBitrate(config.width, config.height, config.fps)
                      : (settings.bitrate > 0 ? settings.bitrate : DEFAULT_BITRATE);
    config.packetSize            = settings.packetSize > 0 ? settings.packetSize : DEFAULT_PACKET_SIZE;
    config.streamingRemotely     = settings.streamingRemotely == STREAM_CFG_REMOTE
            || settings.streamingRemotely == STREAM_CFG_AUTO
            ? settings.streamingRemotely
            : STREAM_CFG_LOCAL;
    config.audioConfiguration    = AUDIO_CONFIGURATION_STEREO;
    config.supportedVideoFormats = VIDEO_FORMAT_H264;
    config.clientRefreshRateX100 = config.fps * 100;
    config.encryptionFlags       = ENCFLG_NONE;
    return config;
}

}
