// hotkey.c
// Implementación de la lógica de hotkeys (almacenamiento, callbacks, lógica principal)
#include "ui_hotkey.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <psp2/ctrl.h>
#include "../connection.h"
#include "../keyboardsystem.h"
#include <psp2/kernel/threadmgr.h>

#define SCE_CTRL_PS 0x1000

hotkey_t g_hotkeys[MAX_HOTKEYS];
int g_hotkey_count = 0;

// Nombres de funciones (de settings_special_names)
extern char *settings_special_names[];

// Prototipos de funciones especiales
static void hotkey_none() {}
static void hotkey_pause_stream() { connection_minimize(); }
static void hotkey_open_keyboard() { keyboardsystem_open_keyboard(); }
static void hotkey_gamepad_buttons() {}
static void hotkey_special_xbox() {}
static void hotkey_lb() {}
static void hotkey_rb() {}
static void hotkey_ls() {}
static void hotkey_rs() {}
static void hotkey_lt() {}
static void hotkey_rt() {}
static void hotkey_mouse_left() {}
static void hotkey_mouse_right() {}
static void hotkey_mouse_middle() {}
static void hotkey_mouse_x1() {}
static void hotkey_mouse_x2() {}
static void hotkey_key_esc() {}
static void hotkey_key_i() {}
static void hotkey_key_m() {}
static void hotkey_key_tab() {}
static void hotkey_key_f1() {}
static void hotkey_key_f2() {}
static void hotkey_key_f3() {}
static void hotkey_key_f4() {}
static void hotkey_key_f5() {}
static void hotkey_key_f6() {}
static void hotkey_key_f7() {}
static void hotkey_key_f8() {}
static void hotkey_key_f9() {}
static void hotkey_key_f10() {}
static void hotkey_key_f11() {}
static void hotkey_key_f12() {}

// Array de callbacks alineado con settings_special_names
static hotkey_func_cb hotkey_callbacks[] = {
    hotkey_none,                // None
    hotkey_none,                // Special inputs (placeholder)
    hotkey_pause_stream,        // Pause stream
    hotkey_open_keyboard,       // Open keyboard
    hotkey_gamepad_buttons,     // Gamepad buttons
    hotkey_special_xbox,        // Special (XBox button)
    hotkey_lb,                  // LB
    hotkey_rb,                  // RB
    hotkey_ls,                  // LS
    hotkey_rs,                  // RS
    hotkey_lt,                  // LT
    hotkey_rt,                  // RT
    hotkey_none,                // Mouse buttons (placeholder)
    hotkey_mouse_left,          // Left
    hotkey_mouse_right,         // Right
    hotkey_mouse_middle,        // Middle(wheel)
    hotkey_mouse_x1,            // X1(4th)
    hotkey_mouse_x2,            // X2(5th)
    hotkey_none,                // Keyboard input codes (placeholder)
    hotkey_key_esc,             // Esc
    hotkey_key_i,               // I
    hotkey_key_m,               // M
    hotkey_key_tab,             // Tab
    hotkey_key_f1,              // F1
    hotkey_key_f2,              // F2
    hotkey_key_f3,              // F3
    hotkey_key_f4,              // F4
    hotkey_key_f5,              // F5
    hotkey_key_f6,              // F6
    hotkey_key_f7,              // F7
    hotkey_key_f8,              // F8
    hotkey_key_f9,              // F9
    hotkey_key_f10,             // F10
    hotkey_key_f11,             // F11
    hotkey_key_f12              // F12
};

void hotkey_init_defaults() {
    memset(g_hotkeys, 0, sizeof(g_hotkeys));
    g_hotkey_count = 0;
}

#define HOTKEYS_CONFIG_PATH "ux0:data/moonlight/hotkeys.conf"

void hotkey_save_config() {
    FILE* f = fopen(HOTKEYS_CONFIG_PATH, "w");
    if (!f) return;
    for (int i = 0; i < g_hotkey_count; ++i) {
        fprintf(f, "hotkey%d_func=%d\n", i, g_hotkeys[i].function);
        fprintf(f, "hotkey%d_count=%d\n", i, g_hotkeys[i].num_buttons);
        for (int b = 0; b < g_hotkeys[i].num_buttons; ++b) {
            fprintf(f, "hotkey%d_btn%d=%u\n", i, b, (unsigned)g_hotkeys[i].buttons[b]);
        }
    }
    fprintf(f, "count=%d\n", g_hotkey_count);
    fclose(f);
}

void hotkey_load_config() {
    FILE* f = fopen(HOTKEYS_CONFIG_PATH, "r");
    if (!f) return;
    char line[128];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "count=%d", &count) == 1) {
            g_hotkey_count = count;
            if (g_hotkey_count > MAX_HOTKEYS) g_hotkey_count = MAX_HOTKEYS;
        }
    }
    rewind(f);
    for (int i = 0; i < g_hotkey_count; ++i) {
        int func = 0, nbtn = 0;
        char key[32];
        snprintf(key, sizeof(key), "hotkey%d_func=%%d", i);
        rewind(f); while (fgets(line, sizeof(line), f)) if (sscanf(line, key, &func) == 1) break;
        snprintf(key, sizeof(key), "hotkey%d_count=%%d", i);
        rewind(f); while (fgets(line, sizeof(line), f)) if (sscanf(line, key, &nbtn) == 1) break;
        g_hotkeys[i].function = func;
        g_hotkeys[i].num_buttons = nbtn;
        for (int b = 0; b < nbtn; ++b) {
            unsigned btn = 0;
            snprintf(key, sizeof(key), "hotkey%d_btn%d=%%u", i, b);
            rewind(f); while (fgets(line, sizeof(line), f)) if (sscanf(line, key, &btn) == 1) break;
            g_hotkeys[i].buttons[b] = btn;
        }
    }
    fclose(f);
}

static bool hotkey_is_pressed(const hotkey_t* hk, const SceCtrlData* pad) {
    int found = 0;
    for (int i = 0; i < hk->num_buttons; ++i) {
        if ((pad->buttons & hk->buttons[i]) != 0) found++;
    }
    return (found == hk->num_buttons);
}

void hotkey_process(const SceCtrlData* pad) {
    static uint32_t last_buttons = 0;
    for (int i = 0; i < g_hotkey_count; ++i) {
        if (hotkey_is_pressed(&g_hotkeys[i], pad) && (last_buttons != pad->buttons)) {
            int func_idx = g_hotkeys[i].function;
            if (func_idx >= 0 && func_idx < (int)(sizeof(hotkey_callbacks)/sizeof(hotkey_func_cb))) {
                if (hotkey_callbacks[func_idx])
                    hotkey_callbacks[func_idx]();
            }
        }
    }
    last_buttons = pad->buttons;
}

__attribute__((constructor))
static void hotkey_autoload() {
    hotkey_load_config();
}
