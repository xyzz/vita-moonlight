// shortcuts.cpp
// Gestión de accesos directos físicos para Vita Moonlight
#include "controller/shortcuts.hpp"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <string.h>

#include "controller/special_inputs.hpp"
#include "debug.hpp"

// Callback para el hotkey de pausa
static std::function<void()> pause_callback = nullptr;

// Estado del hotkey de pausa
static bool pause_shortcut_active         = false;
static uint64_t pause_shortcut_start_time = 0;
static bool pause_start_pressed           = false;
static bool pause_l1_pressed              = false;
static bool pause_r1_pressed              = false;
// Tiempo del último disparo del shortcut (us) para debounce y evitar re-disparos
static uint64_t last_pause_exec_time = 0;

// Establecer callback para pausa
void set_pause_callback(const std::function<void()>& cb)
{
    pause_callback = cb;
    vita_debug_log("Shortcut: callback de pausa establecido");
}

// Devuelve true si se ejecutó un acceso directo y se debe limpiar el input
bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old)
{
    uint64_t now = sceKernelGetSystemTimeWide();
    // Ignorar shortcuts repetidos si acabamos de ejecutar uno (debounce 300ms)
    if (last_pause_exec_time && now - last_pause_exec_time < 300000)
    {
        return false;
    }

    // Verificar botones actuales
    bool start_pressed = (pad->buttons & SCE_CTRL_START);
    bool l1_pressed    = (pad->buttons & SCE_CTRL_L1);
    bool r1_pressed    = (pad->buttons & SCE_CTRL_R1);

    // Log de debug para ver qué botones se están presionando (menos frecuente)
    static int log_counter = 0;
    if (log_counter++ % 300 == 0)
    { // Log cada 5 segundos aproximadamente
        vita_debug_log("Shortcut: botones - START:%d L1:%d R1:%d (0x%08X)",
            start_pressed, l1_pressed, r1_pressed, pad->buttons);
    }

    // Lógica del hotkey START + L1 + R1 con tolerancia temporal
    if (!pause_shortcut_active)
    {
        // Buscar el inicio de la combinación
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
        // Actualizar estado de botones
        if (l1_pressed)
            pause_l1_pressed = true;
        if (r1_pressed)
            pause_r1_pressed = true;

        // Verificar si todos los botones están presionados
        if (pause_start_pressed && pause_l1_pressed && pause_r1_pressed)
        {
            uint64_t elapsed = now - pause_shortcut_start_time;
            if (elapsed < 500000)
            { // 500ms de tolerancia
                vita_debug_log("Shortcut: START+L1+R1 detectado en %llu us, ejecutando callback", elapsed);
                if (pause_callback)
                {
                    vita_debug_log("Shortcut: ejecutando callback de pausa");
                    pause_callback();
                    // Marcar tiempo de ejecución para evitar reentradas rápidas
                    last_pause_exec_time = now;
                }
                else
                {
                    vita_debug_log("Shortcut: callback de pausa es NULL!");
                }
                // Resetear estado
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

        // Resetear si se soltó START
        if (!start_pressed)
        {
            vita_debug_log("Shortcut: START soltado, reseteando estado");
            pause_shortcut_active = false;
            pause_start_pressed   = false;
            pause_l1_pressed      = false;
            pause_r1_pressed      = false;
        }

        // Timeout de seguridad
        if (now - pause_shortcut_start_time > 1000000)
        { // 1 segundo
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
