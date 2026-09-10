#pragma once

#include <array>
#include <cstddef>

#include "ConfigManager.hpp"
#include "Limelight.h"

namespace stream
{

struct Resolution
{
    int width;
    int height;
};

const std::array<Resolution, 8>& supportedStreamResolutions();
const std::array<Resolution, 9>& supportedHostResolutions();
const std::array<int, 5>& supportedFrameRates();
const std::array<int, 9>& supportedBitrates();
bool isSupportedStreamResolution(int width, int height);
std::size_t streamResolutionIndex(int width, int height, std::size_t fallback = 2);
std::size_t hostResolutionIndex(int width, int height, std::size_t fallback = 0);
std::size_t frameRateIndex(int fps, std::size_t fallback = 4);
std::size_t bitrateIndex(int bitrate, std::size_t fallback = 0);
int calculateAutoBitrate(int width, int height, int fps);
STREAM_CONFIGURATION buildConfiguration(const StreamConfiguration& settings);

}
