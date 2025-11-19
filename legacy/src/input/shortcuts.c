// shortcuts.c
// Gestión de accesos directos físicos para Vita Moonlight
#include "shortcuts.h"
#include <psp2/ctrl.h>
#include <string.h>
#include "../keyboardsystem.h"
#include "../connection.h"

// Devuelve true si se ejecutó un acceso directo y se debe limpiar el input
bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old) {
    // Atajo: Start + L1 + R1 para pausar/desplegar menú de pausa
    if ((pad->buttons & SCE_CTRL_START) && (pad->buttons & SCE_CTRL_L1) && (pad->buttons & SCE_CTRL_R1)) {
        vita_debug_log("Shortcut: START+L1+R1 detectado, input se soltará por overlay en vita.c y se abrirá menú de pausa");
        connection_minimize();
        return true;
    }

    // Shortcut Start+Left: detectar en cualquier orden y con margen de tiempo
    static bool keyboard_shortcut_blocked = false;
    static uint64_t shortcut_time = 0;
    static int shortcut_state = 0; // 0: nada, 1: uno presionado, 2: ambos presionados
    // Snapshots eliminados: solo se usan en vita.c
    uint64_t now = sceKernelGetSystemTimeWide();
    bool start_now = (pad->buttons & SCE_CTRL_START);
    bool left_now = (pad->buttons & SCE_CTRL_LEFT);
    bool start_prev = (pad_old->buttons & SCE_CTRL_START);
    bool left_prev = (pad_old->buttons & SCE_CTRL_LEFT);

    // Detectar flanco de subida de cualquiera de los dos
    if ((start_now && !start_prev) || (left_now && !left_prev)) {
        vita_debug_log("Shortcut: Flanco de subida detectado (START=%d, LEFT=%d) en t=%llu", start_now, left_now, now);
        shortcut_time = now;
        shortcut_state = 1;
    }
    // Si ambos están presionados dentro de 300ms, activar shortcut
    if (start_now && left_now) {
        if (shortcut_state == 1 && (now - shortcut_time) < 300000) {
            if (!keyboard_shortcut_blocked) {
                vita_debug_log("Shortcut: START+LEFT detectado en t=%llu (delta=%llu)", now, now-shortcut_time);
                // Limpiar input local y en el host ANTES de abrir el teclado
                // Snapshots eliminados: solo se usan en vita.c
                // Obligatorio porque es bloqueante
                memset((void*)pad, 0, sizeof(SceCtrlData));
                LiSendMultiControllerEvent(0, 1, 0, 0, 0, 128, 0, 128, 0);
                vita_debug_log("[SHORTCUT] Overlay activo: ABRIR teclado, frame vacío enviado al host");
                keyboardsystem_open_keyboard();
                keyboard_shortcut_blocked = true;
                shortcut_state = 0;
                return true;
            }
        }
        shortcut_state = 2;
    }
    if (!start_now && !left_now) {
        if (keyboard_shortcut_blocked || shortcut_state != 0) {
            vita_debug_log("Shortcut: START y LEFT liberados, reseteando estado");
        }
        keyboard_shortcut_blocked = false;
        shortcut_state = 0;
    }
    return false;
}
