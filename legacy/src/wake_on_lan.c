/*
 * Wake-on-LAN para Moonlight Vita
 * Permite enviar paquetes mágicos para encender PCs compatibles desde la Vita
 */

#include "wake_on_lan.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "device.h"

// Envía un paquete Wake-on-LAN a la MAC y broadcast especificados
// mac: dirección MAC en formato XX:XX:XX:XX:XX:XX
// ip_broadcast: dirección de broadcast (ej: "192.168.0.255")
// port: normalmente 9
bool send_wol_packet(const char *mac, const char *ip_broadcast, int port) {
    extern void vita_debug_log(const char *fmt, ...);
    if (!mac || !mac[0]) {
        vita_debug_log("[WOL] MAC vacía o NULL");
        return false;
    }
    uint8_t packet[102];
    int i;
    memset(packet, 0xFF, 6);
    uint8_t mac_bytes[6];
    unsigned int m[6];
    int mac_ok = sscanf(mac, "%x:%x:%x:%x:%x:%x",
               &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
    vita_debug_log("[WOL] MAC de entrada: %s, sscanf=%d", mac, mac_ok);
    if (mac_ok != 6) {
        vita_debug_log("[WOL] Error al parsear la MAC: %s", mac);
        return false;
    }
    for (i = 0; i < 6; i++) mac_bytes[i] = (uint8_t)m[i];
    for (i = 0; i < 16; i++) {
        memcpy(packet + 6 + i * 6, mac_bytes, 6);
    }
    vita_debug_log("[WOL] Paquete armado, enviando a %s:%d", ip_broadcast, port);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        vita_debug_log("[WOL] Error al crear socket UDP");
        return false;
    }
    int broadcast = 1;
    int so = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    vita_debug_log("[WOL] setsockopt SO_BROADCAST = %d", so);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip_broadcast);
    vita_debug_log("[WOL] sockaddr_in: family=%d, port=%d, addr=0x%08x", addr.sin_family, addr.sin_port, addr.sin_addr.s_addr);
    int sent = sendto(sock, packet, sizeof(packet), 0,
                      (struct sockaddr*)&addr, sizeof(addr));
    vita_debug_log("[WOL] sendto devuelto: %d (esperado >=1)", sent);
    close(sock);
    if (sent > 0) {
        vita_debug_log("[WOL] Paquete WOL enviado correctamente");
        return true;
    } else {
        vita_debug_log("[WOL] Error al enviar paquete WOL");
        return false;
    }
}
