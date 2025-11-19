// check_host.c
// Funciones para verificar el estado de los hosts registrados en Moonlight Vita
// Muestra: verde (en línea), rojo (desconectado), amarillo (IP cambió pero es el mismo host)

#include "check_host.h"
#include "device.h"
#include "config.h"
#include "util.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include "debug.h"

#ifdef __vita__
#include "udp_sniffer_vita.h"
#include <psp2/kernel/threadmgr.h>
#define MAX_HOSTS 16

// Variables estáticas para comunicación con el callback
static volatile int mdns_found = 0;
static char mdns_found_ip[64] = "";
static char mdns_hostname_ref[256] = "";

static void mdns_found_cb(int idx, const char* host, const char* pcname, const char* ip, int port) {
    if (strcmp(pcname, mdns_hostname_ref) == 0) {
        strncpy((char*)mdns_found_ip, ip, sizeof(mdns_found_ip)-1);
        mdns_found = 1;
    }
}

// Busca la IP actual de un host por mDNS (sniffer Vita)
bool find_host_ip_mdns(const char *hostname, char *out_ip, size_t out_len) {
    mdns_found = 0;
    mdns_found_ip[0] = '\0';
    strncpy(mdns_hostname_ref, hostname, sizeof(mdns_hostname_ref)-1);
    mdns_hostname_ref[sizeof(mdns_hostname_ref)-1] = '\0';
    udp_sniffer_vita_deinit();
    udp_sniffer_vita_init();
    udp_sniffer_vita_set_callback(mdns_found_cb);
    int ticks = 0, max_ticks = 30; // 3 segundos
    while (!mdns_found && ticks < max_ticks) {
        udp_sniffer_vita_poll();
        sceKernelDelayThread(1000 * 100); // 100ms
        ticks++;
    }
    udp_sniffer_vita_deinit();
    if (mdns_found) {
        strncpy(out_ip, mdns_found_ip, out_len-1);
        out_ip[out_len-1] = '\0';
        return true;
    }
    return false;
}

volatile int g_host_scan_thread_status = 0;
struct host_status g_host_status[MAX_HOSTS];
#define MAX_SCAN_THREADS 8
static SceUID host_scan_thread_ids[MAX_SCAN_THREADS] = {-1};
static int host_scan_thread_count = 0;

// Declaración anticipada para evitar warning/errores
static bool ping_host(const char *ip, uint16_t port);

// Callback estático para el hilo de escaneo de hosts
static void host_scan_mdns_cb(int idx, const char* host, const char* pcname, const char* ip, int port) {
    if (idx < 0 || idx >= MAX_HOSTS) return;
    if (strcmp(pcname, known_devices.devices[idx].name) == 0) {
        strncpy(g_host_status[idx].current_ip, ip, sizeof(g_host_status[idx].current_ip)-1);
        g_host_status[idx].current_ip[sizeof(g_host_status[idx].current_ip)-1] = '\0';
        // Se puede agregar más lógica si se requiere
    }
}

static volatile int scan_mdns_found = 0;
static char scan_mdns_found_ip[64] = "";
static char scan_mdns_hostname_ref[256] = "";

static void scan_mdns_found_cb(int idx, const char* host, const char* pcname, const char* ip, int port) {
    if (strcmp(pcname, scan_mdns_hostname_ref) == 0) {
        strncpy((char*)scan_mdns_found_ip, ip, sizeof(scan_mdns_found_ip)-1);
        scan_mdns_found = 1;
    }
}

volatile int g_host_status_changed = 0;

static int host_scan_thread(SceSize args, void *argp) {
    g_host_scan_thread_status = 1;
    int ticks = 0;
    vita_debug_log("[SCAN] Hilo de escaneo iniciado\n");
    udp_sniffer_vita_deinit();
    udp_sniffer_vita_init();
    while (g_host_scan_thread_status == 1 && ticks < 1000) { // 100s máx
        for (int i = 0; i < known_devices.count && i < MAX_HOSTS; i++) {
            device_info_t *info = &known_devices.devices[i];
            if (!info->paired) {
                if (g_host_status[i].status != HOST_OFFLINE) {
                    vita_debug_log("[SCAN] Host %s marcado como OFFLINE (no paired)\n", info->name);
                    g_host_status_changed = 1;
                }
                g_host_status[i].status = HOST_OFFLINE;
                g_host_status[i].current_ip[0] = '\0';
                continue;
            }
            // --- mDNS polling manual usando variables globales temporales ---
            scan_mdns_found = 0;
            scan_mdns_found_ip[0] = '\0';
            strncpy(scan_mdns_hostname_ref, info->name, sizeof(scan_mdns_hostname_ref)-1);
            scan_mdns_hostname_ref[sizeof(scan_mdns_hostname_ref)-1] = '\0';
            udp_sniffer_vita_set_callback(scan_mdns_found_cb);
            int mdns_ticks = 0;
            while (!scan_mdns_found && mdns_ticks < 10) { // 1s máx
                udp_sniffer_vita_poll();
                sceKernelDelayThread(1000 * 100); // 100ms
                mdns_ticks++;
            }
            udp_sniffer_vita_set_callback(NULL);
            if (scan_mdns_found) {
                vita_debug_log("[SCAN] Host %s encontrado por mDNS: IP=%s\n", info->name, scan_mdns_found_ip);
                if (strcmp(scan_mdns_found_ip, info->internal) == 0) {
                    if (ping_host(scan_mdns_found_ip, info->port)) {
                        if (g_host_status[i].status != HOST_ONLINE) {
                            vita_debug_log("[SCAN] Host %s ONLINE (mDNS IP igual a internal)\n", info->name);
                            g_host_status_changed = 1;
                        }
                        g_host_status[i].status = HOST_ONLINE;
                        strncpy(g_host_status[i].current_ip, scan_mdns_found_ip, sizeof(g_host_status[i].current_ip)-1);
                        g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
                        continue;
                    }
                } else {
                    if (ping_host(scan_mdns_found_ip, info->port)) {
                        if (g_host_status[i].status != HOST_IP_CHANGED) {
                            vita_debug_log("[SCAN] Host %s IP CAMBIADA: old=%s, new=%s\n", info->name, info->internal, scan_mdns_found_ip);
                            g_host_status_changed = 1;
                        }
                        g_host_status[i].status = HOST_IP_CHANGED;
                        strncpy(g_host_status[i].current_ip, scan_mdns_found_ip, sizeof(g_host_status[i].current_ip)-1);
                        g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
                        // Setear flags globales para el menú
                        extern int pending_ip_update_idx;
                        extern char pending_ip_update[64];
                        pending_ip_update_idx = i;
                        strncpy(pending_ip_update, scan_mdns_found_ip, sizeof(pending_ip_update)-1);
                        pending_ip_update[sizeof(pending_ip_update)-1] = '\0';
                        // Detener el hilo de escaneo y sniffer al detectar cambio de IP
                        vita_debug_log("[SCAN] Cambio de IP detectado, deteniendo hilo de escaneo y sniffer\n");
                        g_host_scan_thread_status = 0;
                        udp_sniffer_vita_deinit();
                        return 0; // Salir del hilo inmediatamente
                    }
                }
            }
            if (ping_host(info->internal, info->port)) {
                if (g_host_status[i].status != HOST_ONLINE) {
                    vita_debug_log("[SCAN] Host %s ONLINE (por IP interna)\n", info->name);
                    g_host_status_changed = 1;
                }
                g_host_status[i].status = HOST_ONLINE;
                strncpy(g_host_status[i].current_ip, info->internal, sizeof(g_host_status[i].current_ip)-1);
                g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
                continue;
            }
            if (g_host_status[i].status != HOST_OFFLINE) {
                vita_debug_log("[SCAN] Host %s OFFLINE (no responde)\n", info->name);
                g_host_status_changed = 1;
            }
            g_host_status[i].status = HOST_OFFLINE;
            strncpy(g_host_status[i].current_ip, info->internal, sizeof(g_host_status[i].current_ip)-1);
            g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
        }
        sceKernelDelayThread(1000 * 500); // 500ms
        ticks++;
    }
    udp_sniffer_vita_deinit();
    vita_debug_log("[SCAN] Hilo de escaneo detenido\n");
    g_host_scan_thread_status = 0;
    return 0;
}

void start_host_scan_thread() {
    if (g_host_scan_thread_status == 1) return;
    for (int i = 0; i < MAX_HOSTS; i++) {
        g_host_status[i].status = HOST_OFFLINE;
        g_host_status[i].current_ip[0] = '\0';
    }
    if (host_scan_thread_count >= MAX_SCAN_THREADS) {
        vita_debug_log("[SCAN] Demasiados hilos de escaneo activos, no se creará otro\n");
        return;
    }
    SceUID tid = sceKernelCreateThread("hostscan", host_scan_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (tid >= 0) {
        sceKernelStartThread(tid, 0, 0);
        host_scan_thread_ids[host_scan_thread_count++] = tid;
    }
}

void stop_host_scan_thread() {
    for (int i = 0; i < host_scan_thread_count; i++) {
        SceUID tid = host_scan_thread_ids[i];
        if (tid > 0) {
            g_host_scan_thread_status = 2;
            SceUInt timeout = 100 * 1000; // 100ms
            sceKernelWaitThreadEnd(tid, NULL, &timeout);
            sceKernelDeleteThread(tid);
            host_scan_thread_ids[i] = -1;
        }
    }
    host_scan_thread_count = 0;}
#endif

// El struct host_status y los defines ya están en check_host.h, no se redeclaran aquí

// Intenta conectar al host por IP y puerto, retorna true si responde
static bool ping_host(const char *ip, uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sock);
        return false;
    }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    return res == 0;
}

// Busca la IP actual de un host por nombre (DNS o mDNS)
static bool resolve_host_ip(const char *hostname, char *out_ip, size_t out_len) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    if (getaddrinfo(hostname, NULL, &hints, &res) != 0) return false;
    struct sockaddr_in *addr = (struct sockaddr_in*)res->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, out_ip, out_len);
    freeaddrinfo(res);
    return true;
}

// Chequea el estado de un host registrado
struct host_status check_host_status(const device_info_t *info) {
    struct host_status result = {0};
#ifdef __vita__
    // 1. Intentar mDNS primero
    char mdns_ip[64] = "";
    if (find_host_ip_mdns(info->name, mdns_ip, sizeof(mdns_ip))) {
        if (strcmp(mdns_ip, info->internal) == 0) {
            if (ping_host(mdns_ip, info->port)) {
                result.status = HOST_ONLINE;
                strncpy(result.current_ip, mdns_ip, sizeof(result.current_ip));
                return result;
            }
        } else {
            if (ping_host(mdns_ip, info->port)) {
                result.status = HOST_IP_CHANGED;
                strncpy(result.current_ip, mdns_ip, sizeof(result.current_ip));
                return result;
            }
        }
    }
#endif
    // 2. Intentar ping a la IP registrada
    if (ping_host(info->internal, info->port)) {
        result.status = HOST_ONLINE;
        strncpy(result.current_ip, info->internal, sizeof(result.current_ip));
        return result;
    }
    // 3. Intentar resolver por nombre (DNS estándar)
    if (strcmp(info->name, info->internal) != 0) {
        char resolved_ip[64] = "";
        if (resolve_host_ip(info->name, resolved_ip, sizeof(resolved_ip))) {
            if (ping_host(resolved_ip, info->port)) {
                result.status = HOST_IP_CHANGED;
                strncpy(result.current_ip, resolved_ip, sizeof(result.current_ip));
                return result;
            }
        }
    }
    // 4. No responde
    result.status = HOST_OFFLINE;
    strncpy(result.current_ip, info->internal, sizeof(result.current_ip));
    return result;
}
