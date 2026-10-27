
#include "wake_on_lan.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "device.h"

// Send a Wake-on-LAN packet to the requested MAC and broadcast address.
// port: normalmente 9
bool send_wol_packet(const char* mac, const char* ip_broadcast, int port)
{
    extern void vita_debug_log(const char* fmt, ...);
    if (!mac || !mac[0])
    {
        vita_debug_log("[WOL] MAC is empty or NULL");
        return false;
    }
    uint8_t packet[102];
    int i;
    memset(packet, 0xFF, 6);
    uint8_t mac_bytes[6];
    unsigned int m[6];
    int mac_ok = sscanf(mac, "%x:%x:%x:%x:%x:%x",
        &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
    vita_debug_log("[WOL] Input MAC: %s, sscanf=%d", mac, mac_ok);
    if (mac_ok != 6)
    {
        vita_debug_log("[WOL] Failed to parse MAC: %s", mac);
        return false;
    }
    for (i = 0; i < 6; i++)
        mac_bytes[i] = (uint8_t)m[i];
    for (i = 0; i < 16; i++)
    {
        memcpy(packet + 6 + i * 6, mac_bytes, 6);
    }
    vita_debug_log("[WOL] Packet prepared; sending to %s:%d", ip_broadcast, port);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        vita_debug_log("[WOL] Failed to create UDP socket");
        return false;
    }
    int broadcast = 1;
    int so        = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    vita_debug_log("[WOL] setsockopt SO_BROADCAST = %d", so);
    struct sockaddr_in addr = { 0 };
    addr.sin_family         = AF_INET;
    addr.sin_port           = htons(port);
    addr.sin_addr.s_addr    = inet_addr(ip_broadcast);
    vita_debug_log("[WOL] sockaddr_in: family=%d, port=%d, addr=0x%08x", addr.sin_family, addr.sin_port, addr.sin_addr.s_addr);
    int sent = sendto(sock, packet, sizeof(packet), 0,
        (struct sockaddr*)&addr, sizeof(addr));
    vita_debug_log("[WOL] sendto devuelto: %d (esperado >=1)", sent);
    close(sock);
    if (sent > 0)
    {
        vita_debug_log("[WOL] WOL packet sent");
        return true;
    }
    else
    {
        vita_debug_log("[WOL] Failed to send WOL packet");
        return false;
    }
}
