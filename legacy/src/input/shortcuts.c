// shortcuts.c
// Physical button shortcuts for Moonlight Vita.
#include "shortcuts.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <string.h>

#include "../connection.h"
#include "../debug.h"
#include "../keyboardsystem.h"

// Returns true when a shortcut handled the input and its state must be cleared.
bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old)
{
    // START + L1 + R1 opens the pause menu.
    if ((pad->buttons & SCE_CTRL_START) && (pad->buttons & SCE_CTRL_L1) && (pad->buttons & SCE_CTRL_R1))
    {
        vita_debug_log("Shortcut: START+L1+R1 detected; overlay will release input and open pause menu");
        connection_minimize();
        return true;
    }

    // Start+Left shortcut accepts either button order within the timeout.
    static bool keyboard_shortcut_blocked = false;
    static uint64_t shortcut_time         = 0;
    static int shortcut_state             = 0; // 0: idle, 1: one button pressed, 2: both pressed
    uint64_t now                          = sceKernelGetSystemTimeWide();
    bool start_now                        = (pad->buttons & SCE_CTRL_START);
    bool left_now                         = (pad->buttons & SCE_CTRL_LEFT);
    bool start_prev                       = (pad_old->buttons & SCE_CTRL_START);
    bool left_prev                        = (pad_old->buttons & SCE_CTRL_LEFT);

    // Detect a rising edge on either shortcut button.
    if ((start_now && !start_prev) || (left_now && !left_prev))
    {
        vita_debug_log("Shortcut: rising edge detected (START=%d, LEFT=%d) at t=%llu", start_now, left_now, now);
        shortcut_time  = now;
        shortcut_state = 1;
    }
    // Activate the shortcut when both buttons are pressed within 300 ms.
    if (start_now && left_now)
    {
        if (shortcut_state == 1 && (now - shortcut_time) < 300000)
        {
            if (!keyboard_shortcut_blocked)
            {
                vita_debug_log("Shortcut: START+LEFT detected at t=%llu (delta=%llu)", now, now - shortcut_time);
                // Release input locally and on the host before opening the keyboard.
                // Release input before entering the blocking IME dialog.
                memset((void*)pad, 0, sizeof(SceCtrlData));
                LiSendMultiControllerEvent(0, 1, 0, 0, 0, 128, 0, 128, 0);
                vita_debug_log("[SHORTCUT] Keyboard overlay opening; host input released");
                keyboardsystem_open_keyboard();
                keyboard_shortcut_blocked = true;
                shortcut_state            = 0;
                return true;
            }
        }
        shortcut_state = 2;
    }
    if (!start_now && !left_now)
    {
        if (keyboard_shortcut_blocked || shortcut_state != 0)
        {
            vita_debug_log("Shortcut: START y LEFT liberados, reseteando estado");
        }
        keyboard_shortcut_blocked = false;
        shortcut_state            = 0;
    }
    return false;
}
