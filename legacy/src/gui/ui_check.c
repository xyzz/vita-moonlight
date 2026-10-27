#include "ui_check.h"

#include <stdio.h>
#include <string.h>

#include "../check_host.h"
#include "../debug.h"
#include "guilib.h"

extern int pending_ip_update_idx;
extern char pending_ip_update[64];
#ifdef __vita__
void start_host_scan_thread();
#endif

int ui_check_ip_update(device_info_t* info, const char* new_ip)
{
    vita_debug_log("[UI_CHECK] Al entrar: name=%s, internal=%s, external=%s", info->name, info->internal, info->external);
    // Confirm address changes even if the saved IP is empty.
    char msg[256];
    snprintf(msg, sizeof(msg),
        "Your host changed its IP address.\nOld: %s\nNew: %s",
        info->internal[0] ? info->internal : "(none)", new_ip);
    int confirm = display_confirm(msg);
    vita_debug_log("[UI_CHECK] Host %s display_confirm result: %d", info->name, confirm);
    if (confirm)
    {
        // Update both IP fields before saving a changed host address.
        strncpy(info->internal, new_ip, sizeof(info->internal) - 1);
        info->internal[sizeof(info->internal) - 1] = '\0';
        strncpy(info->external, new_ip, sizeof(info->external) - 1);
        info->external[sizeof(info->external) - 1] = '\0';
        info->prefer_external                      = false;
        info->paired                               = true;
        info->port                                 = info->port ? info->port : 47989;
        vita_debug_log("[UI_CHECK] Before saving: name=%s, internal=%s, external=%s, port=%d, paired=%d, prefer_external=%d", info->name, info->internal, info->external, info->port, info->paired, info->prefer_external);
        save_device_info(info);
        load_all_known_devices();
        device_info_t* updated = find_device(info->name);
        if (updated)
            info = updated;
        vita_debug_log("[UI_CHECK] After saving and reloading: name=%s, internal=%s, external=%s, port=%d, paired=%d, prefer_external=%d", info->name, info->internal, info->external, info->port, info->paired, info->prefer_external);
        flash_message("Host IP updated!");
#ifdef __vita__
        start_host_scan_thread();
#endif
        pending_ip_update_idx = -1;
        pending_ip_update[0]  = '\0';
        return 1;
    }
    else
    {
        vita_debug_log("[UI_CHECK] Host %s IP update cancelled by user.", info->name);
        flash_message("Host IP not updated.");
#ifdef __vita__
        start_host_scan_thread();
#endif
        // Keep pending-IP state until the confirmation flow finishes.
        return 0;
    }
}
