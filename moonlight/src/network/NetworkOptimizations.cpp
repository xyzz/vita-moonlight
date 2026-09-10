#include "network/NetworkOptimizations.hpp"

#include <cstdint>
#include <mutex>

extern "C" uint64_t LiGetMillis();
extern "C" void __real_LiRequestIdrFrame(void);

namespace
{

constexpr std::uint32_t MAX_IDR_DELAY_MS = 2500;

struct IdrState
{
    std::uint64_t lastRequestMs      = 0;
    std::uint64_t burstStartMs       = 0;
    std::uint32_t callsInBurst       = 0;
    std::uint32_t suppressedRequests = 0;
};

bool enabled = true;
IdrState state;
std::mutex stateMutex;

void requestIdrLocked(std::uint64_t now)
{
    __real_LiRequestIdrFrame();
    state.lastRequestMs      = now;
    state.suppressedRequests = 0;
}

void requestIdrSmart()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!enabled)
    {
        __real_LiRequestIdrFrame();
        return;
    }

    const std::uint64_t now = LiGetMillis();
    if (now - state.burstStartMs > 120)
    {
        state.burstStartMs = now;
        state.callsInBurst = 0;
    }
    state.callsInBurst++;

    if (state.lastRequestMs == 0)
    {
        requestIdrLocked(now);
        return;
    }

    const std::uint64_t elapsed = now - state.lastRequestMs;
    const bool urgent           = (state.callsInBurst >= 6 && elapsed > 200)
        || (state.suppressedRequests >= 5 && elapsed > 500);
    if (!urgent && elapsed < MAX_IDR_DELAY_MS)
    {
        state.suppressedRequests++;
        return;
    }

    requestIdrLocked(now);
}

}

extern "C" void vita_netopt_set_enabled(int enable)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    enabled = enable != 0;
}

extern "C" void vita_netopt_force_idr()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    requestIdrLocked(LiGetMillis());
}

extern "C" void __wrap_LiRequestIdrFrame(void)
{
    requestIdrSmart();
}
