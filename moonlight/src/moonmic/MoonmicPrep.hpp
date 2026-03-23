#pragma once

#include <functional>

#include "ConfigManager.hpp"
#include "model/HostStorage.hpp"

namespace moonmic
{

struct PrepCallbacks
{
    std::function<void()> onStart; // Called before handshake begins (set spinner, hide grid)
    std::function<void(bool)> onDone; // Called after handshake completes; parameter = success
    std::function<void()> onCancel; // Called if user cancels at the prompt
};

// Coordinates the resolution prompt and the Moonmic/Sunshine handshake.
// resolutionPromptShown is updated to avoid showing the dialog more than once.
void ensureSunshineReadyWithPrompt(const HostInfo& host,
    const StreamConfiguration& streamCfg,
    const VideoSettings& videoCfg,
    bool& resolutionPromptShown,
    const PrepCallbacks& callbacks);

} // namespace moonmic
