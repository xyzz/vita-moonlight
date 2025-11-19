#include <stdio.h>
#include "ui_check.h"
#include "guilib.h"
#include "../check_host.h"
#include "../debug.h"
#include <string.h>

// Declarar las variables globales para poder limpiarlas aquí
extern int pending_ip_update_idx;
extern char pending_ip_update[64];
#ifdef __vita__
void start_host_scan_thread();
#endif

int ui_check_ip_update(device_info_t *info, const char *new_ip) {
    vita_debug_log("[UI_CHECK] Al entrar: name=%s, internal=%s, external=%s", info->name, info->internal, info->external);
    // Siempre pedir confirmación, aunque internal esté vacío
    char msg[256];
    snprintf(msg, sizeof(msg),
        "Your host changed its IP address.\nOld: %s\nNew: %s",
        info->internal[0] ? info->internal : "(none)", new_ip);
    int confirm = display_confirm(msg);
    vita_debug_log("[UI_CHECK] Host %s display_confirm result: %d", info->name, confirm);
    if (confirm) {
        // Copiar SIEMPRE la nueva IP a ambos campos antes de guardar
        strncpy(info->internal, new_ip, sizeof(info->internal)-1);
        info->internal[sizeof(info->internal)-1] = '\0';
        strncpy(info->external, new_ip, sizeof(info->external)-1);
        info->external[sizeof(info->external)-1] = '\0';
        info->prefer_external = false;
        info->paired = true;
        info->port = info->port ? info->port : 47989;
        vita_debug_log("[UI_CHECK] Antes de guardar: name=%s, internal=%s, external=%s, port=%d, paired=%d, prefer_external=%d", info->name, info->internal, info->external, info->port, info->paired, info->prefer_external);
        save_device_info(info);
        load_all_known_devices();
        device_info_t *updated = find_device(info->name);
        if (updated) info = updated;
        vita_debug_log("[UI_CHECK] Después de guardar y recargar: name=%s, internal=%s, external=%s, port=%d, paired=%d, prefer_external=%d", info->name, info->internal, info->external, info->port, info->paired, info->prefer_external);
        flash_message("Host IP updated!");
#ifdef __vita__
        start_host_scan_thread();
#endif
        pending_ip_update_idx = -1;
        pending_ip_update[0] = '\0';
        return 1;
    } else {
        vita_debug_log("[UI_CHECK] Host %s IP update cancelled by user.", info->name);
        flash_message("Host IP not updated.");
#ifdef __vita__
        start_host_scan_thread();
#endif
        // NO limpiar pending_ip_update_idx ni pending_ip_update aquí
        return 0;
    }
}
