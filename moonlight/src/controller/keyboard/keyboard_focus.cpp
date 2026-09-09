#include "controller/keyboard/keyboard_focus.hpp"

#include <atomic>
#include <borealis/core/thread.hpp>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

#include "ConfigManager.hpp"
#include "debug.hpp"
#include "moonmic/MoonmicBridge.hpp"
#include "session/vita_session.hpp"

namespace
{
std::atomic<std::uint32_t> focusRequestId { 0 };
}

void request_keyboard_typing_focus(KeyboardTypingFocusCallback callback)
{
    const std::uint32_t requestId = focusRequestId.fetch_add(1, std::memory_order_acq_rel) + 1;

    ConfigManager config;
    config.load();
    const VideoSettings settings = config.getVideoSettings();
    std::string host             = settings.microphone_host_ip;
    if (host.empty())
    {
        VitaSession* session = VitaSession::active();
        if (session)
        {
            host = session->address();
        }
    }
    if (host.empty())
    {
        return;
    }

    const int port = settings.microphone_port > 0 ? settings.microphone_port : MOONMIC_DEFAULT_PORT;
    if (!moonmic::MoonmicBridge::getInstance().isAvailable(host, port))
    {
        return;
    }

    std::thread([host, port, requestId, callback = std::move(callback)]()
        {
        const auto result = moonmic::MoonmicBridge::getInstance().queryTypingFocus(host, port);
        if (!result.success)
        {
            vita_log::info("[KeyboardFocus] Moonmic typing focus unavailable");
            return;
        }

        brls::sync([result, requestId, callback]()
            {
            if (focusRequestId.load(std::memory_order_acquire) != requestId) {
                return;
            }
            vita_log::info("[KeyboardFocus] Video focused at y=%.3f (%s)", result.normalized_y,
                result.caret ? "caret" : "pointer");
            if (callback) {
                callback(result.normalized_y);
            } }); })
        .detach();
}

void cancel_keyboard_typing_focus()
{
    focusRequestId.fetch_add(1, std::memory_order_acq_rel);
}
