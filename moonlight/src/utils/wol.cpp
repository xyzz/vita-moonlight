#include "utils/wol.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace utils
{

bool sendWOLPacket(const std::string& mac, const std::string& hostIp)
{
    if (mac.empty())
    {
        return false;
    }

    uint8_t packet[102];
    memset(packet, 0xFF, 6);

    unsigned int m[6];
    int mac_ok = sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x",
        &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
    if (mac_ok != 6)
    {
        mac_ok = sscanf(mac.c_str(), "%x-%x-%x-%x-%x-%x",
            &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
    }

    if (mac_ok != 6)
    {
        return false;
    }

    uint8_t mac_bytes[6];
    for (int i = 0; i < 6; i++)
    {
        mac_bytes[i] = (uint8_t)m[i];
    }

    for (int i = 0; i < 16; i++)
    {
        memcpy(packet + 6 + i * 6, mac_bytes, 6);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        return false;
    }

    int broadcast = 1;
#ifndef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
#else
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));
#endif

    std::vector<std::string> targetIps = { "255.255.255.255" };
    size_t lastDot                     = hostIp.find_last_of('.');
    if (lastDot != std::string::npos)
    {
        targetIps.push_back(hostIp.substr(0, lastDot) + ".255");
    }

    bool success = false;
    for (const auto& ip : targetIps)
    {
        struct sockaddr_in addr = { 0 };
        addr.sin_family         = AF_INET;
        addr.sin_port           = htons(9);
        addr.sin_addr.s_addr    = inet_addr(ip.c_str());

        int sent = sendto(sock, (const char*)packet, sizeof(packet), 0,
            (struct sockaddr*)&addr, sizeof(addr));
        if (sent > 0)
        {
            success = true;
        }
    }

    close(sock);
    return success;
}

} // namespace utils
