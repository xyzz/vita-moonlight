#include "session/connection_telemetry.hpp"

#include <Limelight.h>

#include <atomic>
#include <mutex>

namespace
{

std::atomic<bool> telemetryAvailable { false };
std::mutex telemetryMutex;

}

namespace connection
{

void setTelemetryAvailable(bool available)
{
    telemetryAvailable.store(available, std::memory_order_release);
}

void suspendTelemetry()
{
    telemetryAvailable.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(telemetryMutex);
}

bool getEstimatedRtt(std::uint32_t& estimatedRtt, std::uint32_t& estimatedRttVariance)
{
    std::lock_guard<std::mutex> lock(telemetryMutex);
    if (!telemetryAvailable.load(std::memory_order_acquire))
        return false;
    return LiGetEstimatedRttInfo(&estimatedRtt, &estimatedRttVariance);
}

}
