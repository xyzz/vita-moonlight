// shortcuts.cpp
// Managing physical shortcuts for Vita Moonlight
#include "controller/shortcuts.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <string.h>

#include "controller/special_inputs.hpp"
#include "debug.hpp"

// Callback for pause hotkey
static std::function<void()> pause_callback = nullptr;

// Pause hotkey status
static bool pause_shortcut_active         = false;
static uint64_t pause_shortcut_start_time = 0;
static bool pause_start_pressed           = false;
static bool pause_l1_pressed              = false;
static bool pause_r1_pressed              = false;
// Time of the last shot of the shortcut (us) to debounce and avoid re-shots
static uint64_t last_pause_exec_time = 0;

// Set callback for pause
void set_pause_callback(const std::function<void()>& cb)
{
    pause_callback = cb;
    vita_debug_log("Shortcut: callback de pausa establecido");
}

// Returns true if a shortcut was executed and the input should be cleared
bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old)
{
    uint64_t now = sceKernelGetSystemTimeWide();
    // Ignore repeated shortcuts if we have just executed one (debounce 300ms)
    if (last_pause_exec_time && now - last_pause_exec_time < 300000)
    {
        return false;
    }

    // Check current buttons
    bool start_pressed = (pad->buttons & SCE_CTRL_START);
    bool l1_pressed    = (pad->buttons & SCE_CTRL_L1);
    bool r1_pressed    = (pad->buttons & SCE_CTRL_R1);

    // Debug log to see what buttons are being pressed (less frequent)
    static int log_counter = 0;
    if (log_counter++ % 300 == 0)
    { // Log every 5 seconds approximately
        vita_debug_log("Shortcut: botones - START:%d L1:%d R1:%d (0x%08X)",
            start_pressed, l1_pressed, r1_pressed, pad->buttons);
    }

    // START + L1 + R1 hotkey logic with time tolerance
    if (!pause_shortcut_active)
    {
        // Find the start of the combination
        if (start_pressed && !pause_start_pressed)
        {
            vita_debug_log("Shortcut: START presionado, iniciando timer");
            pause_shortcut_active     = true;
            pause_shortcut_start_time = now;
            pause_start_pressed       = true;
        }
    }

    if (pause_shortcut_active)
    {
        // Update button status
        if (l1_pressed)
            pause_l1_pressed = true;
        if (r1_pressed)
            pause_r1_pressed = true;

        // Check if all buttons are pressed
        if (pause_start_pressed && pause_l1_pressed && pause_r1_pressed)
        {
            uint64_t elapsed = now - pause_shortcut_start_time;
            if (elapsed < 500000)
            { // 500ms tolerance
                vita_debug_log("Shortcut: START+L1+R1 detectado en %llu us, ejecutando callback", elapsed);
                if (pause_callback)
                {
                    vita_debug_log("Shortcut: ejecutando callback de pausa");
                    pause_callback();
                    // Mark execution time to avoid fast reentries
                    last_pause_exec_time = now;
                }
                else
                {
                    vita_debug_log("Shortcut: callback de pausa es NULL!");
                }
                // Reset status
                pause_shortcut_active = false;
                pause_start_pressed   = false;
                pause_l1_pressed      = false;
                pause_r1_pressed      = false;
                return true;
            }
            else
            {
                vita_debug_log("Shortcut: timeout excedido (%llu us), reseteando", elapsed);
                pause_shortcut_active = false;
                pause_start_pressed   = false;
                pause_l1_pressed      = false;
                pause_r1_pressed      = false;
            }
        }

        // Reset if START was released
        if (!start_pressed)
        {
            vita_debug_log("Shortcut: START soltado, reseteando estado");
            pause_shortcut_active = false;
            pause_start_pressed   = false;
            pause_l1_pressed      = false;
            pause_r1_pressed      = false;
        }

        // Security timeout
        if (now - pause_shortcut_start_time > 1000000)
        { // 1 second
            vita_debug_log("Shortcut: timeout de seguridad, reseteando");
            pause_shortcut_active = false;
            pause_start_pressed   = false;
            pause_l1_pressed      = false;
            pause_r1_pressed      = false;
        }
    }

    return false;
}

bool trigger_virtual_shortcut(std::uint32_t specialKey)
{
    switch (specialKey)
    {
        case controller::INPUT_SPECIAL_KEY_PAUSE:
            if (pause_callback)
            {
                pause_callback();
                return true;
            }
            return false;
        case controller::INPUT_SPECIAL_KEY_KEYBOARD:
            vita_debug_log("Shortcut: teclado virtual no implementado aún");
            return false;
        default:
            return false;
    }
}
