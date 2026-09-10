#pragma once

#include <cstdint>

namespace connection
{

void setTelemetryAvailable(bool available);
void suspendTelemetry();
bool getEstimatedRtt(std::uint32_t& estimatedRtt, std::uint32_t& estimatedRttVariance);

}
