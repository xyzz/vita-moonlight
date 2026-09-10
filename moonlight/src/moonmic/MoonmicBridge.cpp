#include "MoonmicBridge.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <borealis.hpp>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "debug.hpp"
#include "model/HostStorage.hpp"

extern "C"
{
#include "moonmic_internal.h"
}

namespace moonmic
{

static_assert(sizeof(moonmic_focus_request_t) == 32);
static_assert(sizeof(moonmic_focus_response_t) == 20);

MoonmicBridge::MoonmicBridge()
{
    loadConfig();
}

MoonmicBridge& MoonmicBridge::getInstance()
{
    static MoonmicBridge instance;
    return instance;
}

void MoonmicBridge::setTargetResolution(uint16_t width, uint16_t height)
{
    if (width < 640 || width > 3840 || height < 480 || height > 2160)
    {
        std::cerr << "[MoonmicBridge] Invalid resolution: " << width << "x" << height << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(target_mutex_);
    target_width_  = width;
    target_height_ = height;

    std::cout << "[MoonmicBridge] Target resolution set: " << width << "x" << height << std::endl;
}

std::pair<uint16_t, uint16_t> MoonmicBridge::getTargetResolution() const
{
    std::lock_guard<std::mutex> lock(target_mutex_);
    return { target_width_, target_height_ };
}

void MoonmicBridge::loadConfig()
{
    ConfigManager config;
    config.load();

    const StreamConfiguration stream = config.getStreamConfig();
    setTargetResolution(stream.width, stream.height);
}

MoonmicBridge::SunshineValidationInfo MoonmicBridge::buildSunshineValidation(const std::string& hostIp)
{
    SunshineValidationInfo info;
    if (hostIp.empty())
    {
        vita_log::warning("[MoonmicBridge] buildSunshineValidation called with empty host");
        return info;
    }

    std::vector<HostInfo> hosts = HostStorage::loadHosts();
    std::string keyDir;

    for (const auto& host : hosts)
    {
        if (host.ip == hostIp)
        {
            if (!GameStreamClient::instance().isConnected(host.ip))
            {
                vita_log::info("[MoonmicBridge] Connecting to %s to populate Sunshine validation", host.name.c_str());
                GameStreamClient::instance().connect(host);
            }
            keyDir = GameStreamClient::instance().getKeyDirFor(host.ip);
            break;
        }
    }

    if (keyDir.empty())
    {
        vita_log::warning("[MoonmicBridge] No keyDir found for %s - PairStatus forced to 0", hostIp.c_str());
        return info;
    }

    info.pair_status         = GameStreamClient::instance().getSunshinePairStatus(hostIp);
    std::string uniqueidPath = keyDir + "/uniqueid.dat";
    FILE* f                  = fopen(uniqueidPath.c_str(), "r");
    if (f)
    {
        char buf[32] = { 0 };
        if (fgets(buf, sizeof(buf), f))
        {
            info.uniqueid = buf;
            while (!info.uniqueid.empty() && (info.uniqueid.back() == '\n' || info.uniqueid.back() == '\r'))
            {
                info.uniqueid.pop_back();
            }
        }
        fclose(f);
    }
    else
    {
        vita_log::warning("[MoonmicBridge] Unable to read uniqueid for %s", hostIp.c_str());
    }

    return info;
}

MoonmicBridge::HandshakeResult MoonmicBridge::sendResolutionHandshake(const std::string& hostIp, int port, bool force)
{
    HandshakeResult result;
    setAvailability(hostIp, port, false);
    if (hostIp.empty() || port <= 0)
    {
        vita_log::error("[MoonmicBridge] Invalid host or port for handshake: %s:%d", hostIp.c_str(), port);
        return result;
    }

    SunshineValidationInfo sunInfo     = buildSunshineValidation(hostIp);
    auto [target_width, target_height] = getTargetResolution();

    moonmic_handshake_t handshake = {};
    handshake.magic               = MOONMIC_HANDSHAKE_MAGIC; // MOON
    handshake.version             = 2;
    handshake.pair_status         = static_cast<uint8_t>(std::clamp(sunInfo.pair_status, 0, 1));

    handshake.uniqueid_len = static_cast<uint8_t>(std::min<size_t>(sunInfo.uniqueid.size(), sizeof(handshake.uniqueid)));
    if (handshake.uniqueid_len > 0)
    {
        memcpy(handshake.uniqueid, sunInfo.uniqueid.data(), handshake.uniqueid_len);
    }

    handshake.devicename_len = static_cast<uint8_t>(std::min<size_t>(sunInfo.devicename.size(), sizeof(handshake.devicename)));
    if (handshake.devicename_len > 0)
    {
        memcpy(handshake.devicename, sunInfo.devicename.data(), handshake.devicename_len);
    }

    handshake.display_width  = target_width;
    handshake.display_height = target_height;

    if (force)
    {
        handshake.flags |= MOONMIC_FLAG_FORCE_UPDATE;
    }

    // Use raw socket to send and wait for ACK
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        vita_log::error("[MoonmicBridge] Failed to create socket for handshake");
        return result;
    }

    struct sockaddr_in addr = {};
    addr.sin_family         = AF_INET;
    addr.sin_port           = htons(port);
    inet_pton(AF_INET, hostIp.c_str(), &addr.sin_addr);

    // Set socket options for PS Vita compatibility
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        vita_log::warning("[MoonmicBridge] Failed to set SO_REUSEADDR (non-fatal)");
    }

    // Set receive timeout (more reliable than poll() on PS Vita)
    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 200000; // 200ms timeout
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        vita_log::warning("[MoonmicBridge] Failed to set SO_RCVTIMEO (non-fatal)");
    }

    // Bind to ephemeral port (port 0) to ensure socket can receive replies
    // This is CRITICAL on PS Vita - without bind, recvfrom may not work
    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family         = AF_INET;
    bind_addr.sin_port           = htons(0); // 0 = OS assigns ephemeral port
    bind_addr.sin_addr.s_addr    = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0)
    {
        vita_log::error("[MoonmicBridge] Failed to bind socket for handshake");
        close(sock);
        return result;
    }

    struct sockaddr_in assigned_addr = {};
    socklen_t addr_len               = sizeof(assigned_addr);
    if (getsockname(sock, (struct sockaddr*)&assigned_addr, &addr_len) == 0)
    {
        uint16_t assigned_port = ntohs(assigned_addr.sin_port);
        vita_log::info("[MoonmicBridge] Socket bound to ephemeral port %d", assigned_port);
    }

    ssize_t sent = sendto(sock, &handshake, sizeof(handshake), 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent != sizeof(handshake))
    {
        vita_log::error("[MoonmicBridge] Failed to send handshake packet");
        close(sock);
        return result;
    }

    vita_log::info("[MoonmicBridge] Handshake sent, waiting for ACK...");

    // Wait for ACK
    // Loop to handle potential PING packets arriving before the ACK
    // Host starts ConnectionMonitor immediately which sends a PING (12 bytes)
    // We need to discard PINGs and wait for the real ACK (~93+ bytes)

    auto start_time = std::chrono::steady_clock::now();

    while (true)
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 300)
        {
            vita_log::info("[MoonmicBridge] Handshake timed out after loop");
            break;
        }

        char buf[1024];
        struct sockaddr_in from_addr = {};
        socklen_t from_len           = sizeof(from_addr);
        int received                 = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from_addr, &from_len);

        if (received > 0)
        {
            uint32_t magic = 0;
            if (received >= 4)
            {
                magic = *(uint32_t*)buf;
            }

            vita_log::info("[MoonmicBridge] Received %d bytes from %s:%d, Magic: 0x%08X", received,
                inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port), magic);

            // Check if it's a PING (12 bytes, magic 0x50494E47)
            if (received == 12)
            {
                if (magic == 0x50494E47)
                { // "PING"
                    vita_log::info("[MoonmicBridge] Received early PING from host, ignoring...");
                    continue; // Continue waiting for ACK
                }
            }

            if (received >= (int)sizeof(moonmic_handshake_t))
            {
                moonmic_handshake_t* ack = (moonmic_handshake_t*)buf;
                if (ack->magic == MOONMIC_HANDSHAKE_ACK)
                {
                    vita_log::info("[MoonmicBridge] Resolution handshake to %s:%d - ACK: RECEIVED, Mismatch: %s, Current: %dx%d",
                        hostIp.c_str(), port,
                        (ack->display_width != target_width || ack->display_height != target_height) ? "YES" : "NO",
                        ack->display_width, ack->display_height);

                    result.success        = true;
                    result.current_width  = ack->display_width;
                    result.current_height = ack->display_height;
                    result.mismatch       = (ack->display_width != target_width || ack->display_height != target_height);
                    setAvailability(hostIp, port, true);

                    close(sock);
                    return result;
                }
                else
                {
                    vita_log::info("[MoonmicBridge] Received packet with wrong magic. Expected 0x%08X (ACK), Got 0x%08X", MOONMIC_HANDSHAKE_ACK, ack->magic);
                }
            }
            else
            {
                vita_log::info("[MoonmicBridge] Packet too small for handshake: %d < %d", received, (int)sizeof(moonmic_handshake_t));
            }
        }
        else
        {
            // If SO_RCVTIMEO or no data
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Time out handled by loop check or socket options
            }
        }
    }

    vita_log::info("[MoonmicBridge] Resolution handshake to %s:%d - ACK: TIMEOUT, Mismatch: NO, Current: 0x0", hostIp.c_str(), port);
    close(sock);
    return result;
}

bool MoonmicBridge::isAvailable(const std::string& hostIp, int port) const
{
    std::lock_guard<std::mutex> lock(availability_mutex_);
    return available_ && available_host_ == hostIp && available_port_ == port;
}

void MoonmicBridge::setAvailability(const std::string& hostIp, int port, bool available)
{
    std::lock_guard<std::mutex> lock(availability_mutex_);
    available_host_ = hostIp;
    available_port_ = port;
    available_      = available;
}

MoonmicBridge::TypingFocusResult MoonmicBridge::queryTypingFocus(const std::string& hostIp, int port)
{
    TypingFocusResult result;
    if (hostIp.empty() || port <= 0 || !isAvailable(hostIp, port))
    {
        return result;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        return result;
    }

    struct timeval timeout = {};
    timeout.tv_usec        = 120000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in bindAddr = {};
    bindAddr.sin_family         = AF_INET;
    bindAddr.sin_port           = htons(0);
    bindAddr.sin_addr.s_addr    = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0)
    {
        close(sock);
        return result;
    }

    struct sockaddr_in hostAddr = {};
    hostAddr.sin_family         = AF_INET;
    hostAddr.sin_port           = htons(port);
    if (inet_pton(AF_INET, hostIp.c_str(), &hostAddr.sin_addr) != 1)
    {
        close(sock);
        return result;
    }

    moonmic_focus_request_t request         = {};
    request.magic                           = MOONMIC_FOCUS_REQUEST_MAGIC;
    request.version                         = MOONMIC_FOCUS_PROTOCOL_VERSION;
    const SunshineValidationInfo validation = buildSunshineValidation(hostIp);
    request.pair_status                     = static_cast<uint8_t>(std::clamp(validation.pair_status, 0, 1));
    request.uniqueid_len                    = static_cast<uint8_t>(std::min(validation.uniqueid.size(), sizeof(request.uniqueid)));
    if (request.uniqueid_len > 0)
    {
        memcpy(request.uniqueid, validation.uniqueid.data(), request.uniqueid_len);
    }
    request.request_id = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    if (sendto(sock, &request, sizeof(request), 0, reinterpret_cast<struct sockaddr*>(&hostAddr), sizeof(hostAddr)) != sizeof(request))
    {
        close(sock);
        return result;
    }

    moonmic_focus_response_t response = {};
    struct sockaddr_in responseAddr   = {};
    socklen_t responseAddrLen         = sizeof(responseAddr);
    const int received                = recvfrom(sock, &response, sizeof(response), 0,
                       reinterpret_cast<struct sockaddr*>(&responseAddr), &responseAddrLen);
    close(sock);

    const bool validSource = response.source == MOONMIC_FOCUS_SOURCE_CARET || response.source == MOONMIC_FOCUS_SOURCE_POINTER;
    if (received != sizeof(response) || responseAddr.sin_addr.s_addr != hostAddr.sin_addr.s_addr || responseAddr.sin_port != hostAddr.sin_port || response.magic != MOONMIC_FOCUS_RESPONSE_MAGIC || response.version != MOONMIC_FOCUS_PROTOCOL_VERSION || response.request_id != request.request_id || !validSource)
    {
        return result;
    }

    result.success      = true;
    result.caret        = response.source == MOONMIC_FOCUS_SOURCE_CARET;
    result.normalized_x = static_cast<float>(response.normalized_x) / 65535.0f;
    result.normalized_y = static_cast<float>(response.normalized_y) / 65535.0f;
    return result;
}

} // namespace moonmic
