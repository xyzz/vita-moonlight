#pragma once

#include <string>

namespace utils
{

// Sends a Wake-on-LAN magic packet to the specified MAC address and subnet broadcast IP calculated from hostIp.
bool sendWOLPacket(const std::string& mac, const std::string& hostIp);

} // namespace utils
