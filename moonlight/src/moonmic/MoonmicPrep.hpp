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

// Coordina el prompt de resolución y el handshake Moonmic/Sunshine.
// resolutionPromptShown se actualiza para evitar mostrar el diálogo más de una vez.
void ensureSunshineReadyWithPrompt(const HostInfo& host,
    const StreamConfiguration& streamCfg,
    const VideoSettings& videoCfg,
    bool& resolutionPromptShown,
    const PrepCallbacks& callbacks);

} // namespace moonmic
