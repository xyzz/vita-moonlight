#pragma once

#include <functional>

#include "ConfigManager.hpp"
#include "model/HostStorage.hpp"

namespace moonmic
{

struct PrepCallbacks
{
    std::function<void()> onStart;
    std::function<void(bool)> onDone;
};

void prepareSunshine(const HostInfo& host,
    const StreamConfiguration& streamCfg,
    const VideoSettings& videoCfg,
    const PrepCallbacks& callbacks);

} // namespace moonmic
